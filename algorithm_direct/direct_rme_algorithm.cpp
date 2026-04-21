#include "direct_rme_algorithm.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
constexpr std::uint64_t kSeedBase = 0x2C9277B5E57A4A83ull;

double wrapPhaseRad(double phase) {
    double wrapped = std::fmod(phase, kTwoPi);
    if (wrapped < 0.0) {
        wrapped += kTwoPi;
    }
    return wrapped;
}

void seedMix(std::uint64_t &seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

std::uint32_t buildDeterministicSeed(int width,
                                     int height,
                                     const std::vector<std::pair<int, int>> &targetOffsets) {
    std::uint64_t seed = kSeedBase;
    seedMix(seed, static_cast<std::uint64_t>(width));
    seedMix(seed, static_cast<std::uint64_t>(height));
    seedMix(seed, static_cast<std::uint64_t>(targetOffsets.size()));
    for (const auto &offset : targetOffsets) {
        seedMix(seed, static_cast<std::uint64_t>(static_cast<std::int64_t>(offset.first)));
        seedMix(seed, static_cast<std::uint64_t>(static_cast<std::int64_t>(offset.second)));
    }

    std::uint32_t out = static_cast<std::uint32_t>((seed >> 32u) ^ (seed & 0xffffffffu));
    if (out == 0u) {
        out = 0xA511E9B3u;
    }
    return out;
}

bool prepareMappedOffsets(const AlgorithmDirect::DirectRmeVariables &variables,
                          const QVector<AlgorithmDirect::DirectRmeTargetPoint> &targets,
                          AlgorithmDirect::DirectRmeResult &result,
                          std::vector<std::pair<int, int>> &mappedOffsets) {
    result = AlgorithmDirect::DirectRmeResult();
    result.requestedTargetCount = targets.size();

    if (variables.slmWidth <= 0 || variables.slmHeight <= 0) {
        result.error = "Invalid SLM resolution for RME algorithm.";
        return false;
    }
    if (variables.camWidth <= 0 || variables.camHeight <= 0) {
        result.error = "Invalid camera resolution for RME algorithm.";
        return false;
    }
    if (variables.slmPixelSizeUm <= 0.0 || variables.camPixelSizeUm <= 0.0 ||
        variables.cameraImagingMagnification <= 0.0 ||
        variables.wavelengthNm <= 0.0 || variables.focalLengthMm <= 0.0) {
        result.error = "Optical/hardware settings must be positive before running RME.";
        return false;
    }
    if (targets.isEmpty()) {
        result.error = "No target points found. Add at least one point on the target grid.";
        return false;
    }

    const double slmDx = variables.slmPixelSizeUm * 1e-6;
    const double slmDy = variables.slmPixelSizeUm * 1e-6;
    const double effectiveCamPixelSizeUm = variables.camPixelSizeUm / variables.cameraImagingMagnification;
    const double camDx = effectiveCamPixelSizeUm * 1e-6;
    const double camDy = effectiveCamPixelSizeUm * 1e-6;
    const double wavelength = variables.wavelengthNm * 1e-9;
    const double focalLength = variables.focalLengthMm * 1e-3;

    const double focalDx = (wavelength * focalLength) / (static_cast<double>(variables.slmWidth) * slmDx);
    const double focalDy = (wavelength * focalLength) / (static_cast<double>(variables.slmHeight) * slmDy);
    if (focalDx == 0.0 || focalDy == 0.0) {
        result.error = "Computed focal-plane pixel pitch is zero. Check optical settings.";
        return false;
    }

    const int cx = variables.slmWidth / 2;
    const int cy = variables.slmHeight / 2;

    std::set<std::pair<int, int>> dedupOffsets;
    for (const AlgorithmDirect::DirectRmeTargetPoint &target : targets) {
        if (std::abs(target.xCamPx) > static_cast<double>(variables.camWidth) / 2.0 ||
            std::abs(target.yCamPx) > static_cast<double>(variables.camHeight) / 2.0) {
            ++result.skippedOutsideCameraFov;
            continue;
        }

        const double physX = target.xCamPx * camDx;
        const double physY = target.yCamPx * camDy;
        const int fftOffsetX = static_cast<int>(std::llround(physX / focalDx));
        const int fftOffsetY = static_cast<int>(std::llround(physY / focalDy));

        const int fftX = cx + fftOffsetX;
        const int fftY = cy - fftOffsetY;
        if (fftX < 0 || fftX >= variables.slmWidth || fftY < 0 || fftY >= variables.slmHeight) {
            ++result.skippedOutsideSlmBounds;
            continue;
        }

        dedupOffsets.insert({fftOffsetX, fftOffsetY});
    }

    if (dedupOffsets.empty()) {
        result.error = "No valid target points remained after camera/Fourier mapping.";
        return false;
    }

    mappedOffsets.assign(dedupOffsets.begin(), dedupOffsets.end());
    result.usedTargetCount = static_cast<int>(mappedOffsets.size());
    return true;
}
} // namespace

namespace AlgorithmDirect {

QVector<DirectRmeTargetPoint> buildRmePointTargets(const QVector<QPointF> &cameraCenteredPoints) {
    QVector<DirectRmeTargetPoint> targets;
    targets.reserve(cameraCenteredPoints.size());

    for (const QPointF &point : cameraCenteredPoints) {
        DirectRmeTargetPoint target;
        target.xCamPx = point.x();
        target.yCamPx = point.y();
        targets.append(target);
    }

    return targets;
}

DirectRmeResult runRandomMaskEncoding(const DirectRmeVariables &variables,
                                      const QVector<DirectRmeTargetPoint> &targets) {
    DirectRmeResult result;
    std::vector<std::pair<int, int>> targetOffsets;
    if (!prepareMappedOffsets(variables, targets, result, targetOffsets)) {
        return result;
    }

    const int width = variables.slmWidth;
    const int height = variables.slmHeight;
    const int pixelCount = width * height;
    const int trapCount = static_cast<int>(targetOffsets.size());
    if (pixelCount <= 0 || trapCount <= 0) {
        result.error = "Invalid RME internal dimensions.";
        return result;
    }

    // Deterministically assign SLM pixels to target traps in random order.
    std::vector<int> pixelOrder(static_cast<std::size_t>(pixelCount));
    std::iota(pixelOrder.begin(), pixelOrder.end(), 0);
    std::mt19937 rng(buildDeterministicSeed(width, height, targetOffsets));
    std::shuffle(pixelOrder.begin(), pixelOrder.end(), rng);

    std::vector<int> trapForPixel(static_cast<std::size_t>(pixelCount), 0);
    int cursor = 0;
    const int baseCount = pixelCount / trapCount;
    const int remainder = pixelCount % trapCount;

    for (int trapIndex = 0; trapIndex < trapCount; ++trapIndex) {
        const int quota = baseCount + ((trapIndex < remainder) ? 1 : 0);
        for (int j = 0; j < quota && cursor < pixelCount; ++j) {
            trapForPixel[static_cast<std::size_t>(pixelOrder[static_cast<std::size_t>(cursor)])] = trapIndex;
            ++cursor;
        }
    }

    result.wrappedPhaseRad.resize(pixelCount);
    result.phaseMask8Bit = QImage(width, height, QImage::Format_Grayscale8);
    if (result.phaseMask8Bit.isNull()) {
        result.error = "Failed to allocate RME phase mask image.";
        return result;
    }

    const double invWidth = 1.0 / static_cast<double>(width);
    const double invHeight = 1.0 / static_cast<double>(height);
    for (int y = 0; y < height; ++y) {
        uchar *row = result.phaseMask8Bit.scanLine(y);
        const int rowBase = y * width;
        for (int x = 0; x < width; ++x) {
            const int idx = rowBase + x;
            const int trapIndex = trapForPixel[static_cast<std::size_t>(idx)];
            const auto &offset = targetOffsets[static_cast<std::size_t>(trapIndex)];

            const double phase = kTwoPi * (static_cast<double>(offset.first) * static_cast<double>(x) * invWidth -
                                           static_cast<double>(offset.second) * static_cast<double>(y) * invHeight);
            const double wrapped = wrapPhaseRad(phase);
            result.wrappedPhaseRad[idx] = wrapped;

            const double scaled = (wrapped / kTwoPi) * 255.0;
            row[x] = static_cast<uchar>(std::clamp(static_cast<int>(scaled), 0, 255));
        }
    }

    result.success = true;
    return result;
}

DirectRmeResult generateRandomMaskEncodingPhaseMask(const QVector<QPointF> &cameraCenteredPoints,
                                                    const DirectRmeVariables &variables) {
    DirectRmeResult result;
    if (cameraCenteredPoints.isEmpty()) {
        result.error = "No camera-centered target points were provided.";
        return result;
    }

    return runRandomMaskEncoding(variables, buildRmePointTargets(cameraCenteredPoints));
}

} // namespace AlgorithmDirect
