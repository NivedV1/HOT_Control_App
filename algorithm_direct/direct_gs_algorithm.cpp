#include "direct_gs_algorithm.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float kPiF = 3.14159265358979323846f;

cv::Mat roll2d(const cv::Mat &input, int shiftY, int shiftX) {
    if (input.empty()) {
        return cv::Mat();
    }

    const int rows = input.rows;
    const int cols = input.cols;
    if (rows == 0 || cols == 0) {
        return input.clone();
    }

    int y = shiftY % rows;
    int x = shiftX % cols;
    if (y < 0) {
        y += rows;
    }
    if (x < 0) {
        x += cols;
    }

    if (y == 0 && x == 0) {
        return input.clone();
    }

    cv::Mat shiftedRows(input.size(), input.type());
    if (y == 0) {
        input.copyTo(shiftedRows);
    } else {
        input.rowRange(rows - y, rows).copyTo(shiftedRows.rowRange(0, y));
        input.rowRange(0, rows - y).copyTo(shiftedRows.rowRange(y, rows));
    }

    cv::Mat shifted(input.size(), input.type());
    if (x == 0) {
        shiftedRows.copyTo(shifted);
    } else {
        shiftedRows.colRange(cols - x, cols).copyTo(shifted.colRange(0, x));
        shiftedRows.colRange(0, cols - x).copyTo(shifted.colRange(x, cols));
    }

    return shifted;
}

cv::Mat fftshift2d(const cv::Mat &input) {
    return roll2d(input, input.rows / 2, input.cols / 2);
}

cv::Mat ifftshift2d(const cv::Mat &input) {
    return roll2d(input, -(input.rows / 2), -(input.cols / 2));
}

double wrapPhaseRad(double phase) {
    double wrapped = std::fmod(phase, kTwoPi);
    if (wrapped < 0.0) {
        wrapped += kTwoPi;
    }
    return wrapped;
}

struct PreparedGsData {
    QVector<float> targetAmplitude;
    QVector<float> initialPhaseRad;
};

bool validateCommonInputs(const AlgorithmDirect::DirectGsVariables &variables,
                          const QVector<float> &sourceAmplitude,
                          AlgorithmDirect::DirectGsResult &result) {
    result = AlgorithmDirect::DirectGsResult();

    if (variables.slmWidth <= 0 || variables.slmHeight <= 0) {
        result.error = "Invalid SLM resolution for GS algorithm.";
        return false;
    }
    if (variables.camWidth <= 0 || variables.camHeight <= 0) {
        result.error = "Invalid camera resolution for GS algorithm.";
        return false;
    }
    if (variables.iterations <= 0) {
        result.error = "Iterations must be greater than zero.";
        return false;
    }
    if (variables.slmPixelSizeUm <= 0.0 || variables.camPixelSizeUm <= 0.0 ||
        variables.cameraImagingMagnification <= 0.0 ||
        variables.wavelengthNm <= 0.0 || variables.focalLengthMm <= 0.0) {
        result.error = "Optical/hardware settings must be positive before running GS.";
        return false;
    }

    const int pixelCount = variables.slmWidth * variables.slmHeight;
    if (sourceAmplitude.size() != pixelCount) {
        result.error = "Source amplitude map size does not match SLM resolution.";
        return false;
    }

    return true;
}

void computeMappingScales(const AlgorithmDirect::DirectGsVariables &variables,
                          double &camDx,
                          double &camDy,
                          double &focalDx,
                          double &focalDy) {
    const double slmDx = variables.slmPixelSizeUm * 1e-6;
    const double slmDy = variables.slmPixelSizeUm * 1e-6;
    const double effectiveCamPixelSizeUm = variables.camPixelSizeUm / variables.cameraImagingMagnification;
    camDx = effectiveCamPixelSizeUm * 1e-6;
    camDy = effectiveCamPixelSizeUm * 1e-6;
    const double wavelength = variables.wavelengthNm * 1e-9;
    const double focalLength = variables.focalLengthMm * 1e-3;

    focalDx = (wavelength * focalLength) / (static_cast<double>(variables.slmWidth) * slmDx);
    focalDy = (wavelength * focalLength) / (static_cast<double>(variables.slmHeight) * slmDy);
}

void initializePhase(const AlgorithmDirect::DirectGsVariables &variables,
                     PreparedGsData &prepared) {
    const int pixelCount = variables.slmWidth * variables.slmHeight;
    prepared.initialPhaseRad.resize(pixelCount);

    switch (variables.startingPhaseMask) {
    case AlgorithmDirect::DirectGsStartingPhaseMask::RandomPhase: {
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> phaseDist(-kPiF, kPiF);
        for (int idx = 0; idx < pixelCount; ++idx) {
            prepared.initialPhaseRad[idx] = phaseDist(rng);
        }
        break;
    }
    case AlgorithmDirect::DirectGsStartingPhaseMask::BinaryGrating:
        for (int y = 0; y < variables.slmHeight; ++y) {
            const int rowBase = y * variables.slmWidth;
            for (int x = 0; x < variables.slmWidth; ++x) {
                const int idx = rowBase + x;
                prepared.initialPhaseRad[idx] = (x % 2 == 0) ? 0.0f : kPiF;
            }
        }
        break;
    case AlgorithmDirect::DirectGsStartingPhaseMask::Checkerboard:
    default:
        for (int y = 0; y < variables.slmHeight; ++y) {
            const int rowBase = y * variables.slmWidth;
            for (int x = 0; x < variables.slmWidth; ++x) {
                const int idx = rowBase + x;
                prepared.initialPhaseRad[idx] = ((x + y) % 2 == 0) ? 0.0f : kPiF;
            }
        }
        break;
    }
}

bool preparePointTargetData(const AlgorithmDirect::DirectGsVariables &variables,
                            const QVector<float> &sourceAmplitude,
                            const QVector<AlgorithmDirect::DirectGsTargetPoint> &targets,
                            AlgorithmDirect::DirectGsResult &result,
                            PreparedGsData &prepared) {
    if (!validateCommonInputs(variables, sourceAmplitude, result)) {
        return false;
    }
    result.requestedTargetCount = targets.size();

    if (targets.isEmpty()) {
        result.error = "No target points found. Add at least one point on the target grid.";
        return false;
    }

    double camDx = 0.0;
    double camDy = 0.0;
    double focalDx = 0.0;
    double focalDy = 0.0;
    computeMappingScales(variables, camDx, camDy, focalDx, focalDy);
    if (focalDx == 0.0 || focalDy == 0.0) {
        result.error = "Computed focal-plane pixel pitch is zero. Check optical settings.";
        return false;
    }

    const int pixelCount = variables.slmWidth * variables.slmHeight;
    prepared.targetAmplitude.fill(0.0f, pixelCount);

    const int cx = variables.slmWidth / 2;
    const int cy = variables.slmHeight / 2;

    for (const AlgorithmDirect::DirectGsTargetPoint &target : targets) {
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

        const int idx = fftY * variables.slmWidth + fftX;
        if (prepared.targetAmplitude[idx] == 0.0f) {
            ++result.usedTargetCount;
        }
        prepared.targetAmplitude[idx] = 1.0f;
    }

    if (result.usedTargetCount == 0) {
        result.error = "No valid target points remained after camera/Fourier mapping.";
        return false;
    }

    initializePhase(variables, prepared);
    return true;
}

bool prepareDenseTargetData(const AlgorithmDirect::DirectGsVariables &variables,
                            const QVector<float> &sourceAmplitude,
                            const AlgorithmDirect::DirectDenseTargetImage &denseTargetImage,
                            AlgorithmDirect::DirectGsResult &result,
                            PreparedGsData &prepared) {
    if (!validateCommonInputs(variables, sourceAmplitude, result)) {
        return false;
    }

    if (denseTargetImage.width <= 0 || denseTargetImage.height <= 0) {
        result.error = "Loaded target image has invalid dimensions.";
        return false;
    }
    if (denseTargetImage.width != variables.camWidth || denseTargetImage.height != variables.camHeight) {
        result.error = "Loaded target image must match the current camera resolution before running GS.";
        return false;
    }
    if (denseTargetImage.amplitude.size() != denseTargetImage.width * denseTargetImage.height) {
        result.error = "Loaded target image amplitude size does not match its dimensions.";
        return false;
    }

    double camDx = 0.0;
    double camDy = 0.0;
    double focalDx = 0.0;
    double focalDy = 0.0;
    computeMappingScales(variables, camDx, camDy, focalDx, focalDy);
    if (focalDx == 0.0 || focalDy == 0.0) {
        result.error = "Computed focal-plane pixel pitch is zero. Check optical settings.";
        return false;
    }

    const int pixelCount = variables.slmWidth * variables.slmHeight;
    prepared.targetAmplitude.fill(0.0f, pixelCount);

    const int cx = variables.slmWidth / 2;
    const int cy = variables.slmHeight / 2;
    float maxAccumulatedAmplitude = 0.0f;

    for (int y = 0; y < denseTargetImage.height; ++y) {
        const double yCamPx = (static_cast<double>(denseTargetImage.height) / 2.0) - static_cast<double>(y);
        const double physY = yCamPx * camDy;
        const int fftOffsetY = static_cast<int>(std::llround(physY / focalDy));
        const int fftY = cy - fftOffsetY;

        for (int x = 0; x < denseTargetImage.width; ++x) {
            const int srcIdx = y * denseTargetImage.width + x;
            const float amplitude = std::clamp(denseTargetImage.amplitude[srcIdx], 0.0f, 1.0f);
            if (amplitude <= 0.0f) {
                continue;
            }

            ++result.requestedTargetCount;
            const double xCamPx = static_cast<double>(x) - (static_cast<double>(denseTargetImage.width) / 2.0);
            const double physX = xCamPx * camDx;
            const int fftOffsetX = static_cast<int>(std::llround(physX / focalDx));
            const int fftX = cx + fftOffsetX;

            if (fftX < 0 || fftX >= variables.slmWidth || fftY < 0 || fftY >= variables.slmHeight) {
                ++result.skippedOutsideSlmBounds;
                continue;
            }

            const int idx = fftY * variables.slmWidth + fftX;
            const float accumulated = prepared.targetAmplitude[idx] + amplitude;
            if (prepared.targetAmplitude[idx] == 0.0f) {
                ++result.usedTargetCount;
            }
            prepared.targetAmplitude[idx] = accumulated;
            maxAccumulatedAmplitude = std::max(maxAccumulatedAmplitude, accumulated);
        }
    }

    if (result.requestedTargetCount == 0) {
        result.error = "Loaded target image is empty after grayscale normalization.";
        return false;
    }
    if (result.usedTargetCount == 0 || maxAccumulatedAmplitude <= 0.0f) {
        result.error = "No valid image target pixels remained after camera/Fourier mapping.";
        return false;
    }

    for (float &value : prepared.targetAmplitude) {
        if (value > 0.0f) {
            value /= maxAccumulatedAmplitude;
        }
    }

    initializePhase(variables, prepared);
    return true;
}

void populatePhaseImage(const QVector<float> &phaseRad,
                        int width,
                        int height,
                        AlgorithmDirect::DirectGsResult &result) {
    result.wrappedPhaseRad.resize(width * height);
    result.phaseMask8Bit = QImage(width, height, QImage::Format_Grayscale8);

    for (int y = 0; y < height; ++y) {
        uchar *row = result.phaseMask8Bit.scanLine(y);
        const int rowBase = y * width;
        for (int x = 0; x < width; ++x) {
            const int idx = rowBase + x;
            const double wrapped = wrapPhaseRad(static_cast<double>(phaseRad[idx]));
            result.wrappedPhaseRad[idx] = wrapped;

            const double scaled = (wrapped / kTwoPi) * 255.0;
            row[x] = static_cast<uchar>(std::clamp(static_cast<int>(scaled), 0, 255));
        }
    }
}

AlgorithmDirect::DirectGsResult runPreparedGsCpu(const AlgorithmDirect::DirectGsVariables &variables,
                                                 const QVector<float> &sourceAmplitude,
                                                 const PreparedGsData &prepared,
                                                 const AlgorithmDirect::DirectGsResult &baseResult) {
    AlgorithmDirect::DirectGsResult result = baseResult;
    const int pixelCount = variables.slmWidth * variables.slmHeight;

    QVector<double> slmPhase(pixelCount, 0.0);
    for (int i = 0; i < pixelCount; ++i) {
        slmPhase[i] = static_cast<double>(prepared.initialPhaseRad[i]);
    }

    cv::Mat slmField(variables.slmHeight, variables.slmWidth, CV_64FC2);
    cv::Mat focalField;
    cv::Mat focalModified(variables.slmHeight, variables.slmWidth, CV_64FC2);
    cv::Mat slmFieldNew;

    for (int iter = 0; iter < variables.iterations; ++iter) {
        // Build SLM plane complex field: source amplitude * exp(i * current phase).
        for (int y = 0; y < variables.slmHeight; ++y) {
            cv::Vec2d *row = slmField.ptr<cv::Vec2d>(y);
            const int rowBase = y * variables.slmWidth;
            for (int x = 0; x < variables.slmWidth; ++x) {
                const int idx = rowBase + x;
                const double amp = static_cast<double>(sourceAmplitude[idx]);
                row[x][0] = amp * std::cos(slmPhase[idx]);
                row[x][1] = amp * std::sin(slmPhase[idx]);
            }
        }

        // Forward Fourier transform from SLM plane to focal plane.
        cv::dft(ifftshift2d(slmField), focalField, cv::DFT_COMPLEX_OUTPUT);
        focalField = fftshift2d(focalField);

        // Keep focal-plane phase, replace focal-plane amplitude with target amplitude.
        for (int y = 0; y < variables.slmHeight; ++y) {
            const cv::Vec2d *srcRow = focalField.ptr<cv::Vec2d>(y);
            cv::Vec2d *dstRow = focalModified.ptr<cv::Vec2d>(y);
            const int rowBase = y * variables.slmWidth;
            for (int x = 0; x < variables.slmWidth; ++x) {
                const int idx = rowBase + x;
                const double phase = std::atan2(srcRow[x][1], srcRow[x][0]);
                const double amp = static_cast<double>(prepared.targetAmplitude[idx]);
                dstRow[x][0] = amp * std::cos(phase);
                dstRow[x][1] = amp * std::sin(phase);
            }
        }

        // Inverse Fourier transform back to SLM plane, then keep only phase.
        cv::dft(ifftshift2d(focalModified),
                slmFieldNew,
                cv::DFT_INVERSE | cv::DFT_COMPLEX_OUTPUT | cv::DFT_SCALE);
        slmFieldNew = fftshift2d(slmFieldNew);

        for (int y = 0; y < variables.slmHeight; ++y) {
            const cv::Vec2d *row = slmFieldNew.ptr<cv::Vec2d>(y);
            const int rowBase = y * variables.slmWidth;
            for (int x = 0; x < variables.slmWidth; ++x) {
                const int idx = rowBase + x;
                slmPhase[idx] = std::atan2(row[x][1], row[x][0]);
            }
        }
    }

    QVector<float> outputPhase(pixelCount, 0.0f);
    for (int i = 0; i < pixelCount; ++i) {
        outputPhase[i] = static_cast<float>(slmPhase[i]);
    }

    populatePhaseImage(outputPhase, variables.slmWidth, variables.slmHeight, result);
    result.success = true;
    return result;
}
} // namespace

namespace AlgorithmDirect {

QVector<float> buildGaussianSourceAmplitude(int width, int height, double beamWaistPx) {
    QVector<float> source;
    if (width <= 0 || height <= 0) {
        return source;
    }

    source.resize(width * height);
    const double cx = (static_cast<double>(width) - 1.0) * 0.5;
    const double cy = (static_cast<double>(height) - 1.0) * 0.5;
    const double sigma = std::max(1e-6, beamWaistPx);
    const double denom = 2.0 * sigma * sigma;

    for (int y = 0; y < height; ++y) {
        const int rowBase = y * width;
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            source[rowBase + x] = static_cast<float>(std::exp(-(dx * dx + dy * dy) / denom));
        }
    }

    return source;
}

QVector<float> buildSourceAmplitude(const DirectGsVariables &variables, bool *usedDefaultSource) {
    const int expectedSize = variables.slmWidth * variables.slmHeight;
    if (variables.sourceAmplitude.size() == expectedSize) {
        if (usedDefaultSource) {
            *usedDefaultSource = false;
        }
        return variables.sourceAmplitude;
    }

    if (usedDefaultSource) {
        *usedDefaultSource = true;
    }
    const double waist = variables.defaultSourceBeamWaistPx > 0.0
        ? variables.defaultSourceBeamWaistPx
        : static_cast<double>(std::min(variables.slmWidth, variables.slmHeight)) / 6.0;
    return buildGaussianSourceAmplitude(variables.slmWidth, variables.slmHeight, waist);
}

QImage resizeImageToCameraGrayscale(const QImage &image, const DirectGsVariables &variables) {
    if (image.isNull()) {
        return QImage();
    }

    return image.convertToFormat(QImage::Format_Grayscale8).scaled(
        variables.camWidth,
        variables.camHeight,
        Qt::IgnoreAspectRatio,
        Qt::SmoothTransformation);
}

DirectDenseTargetImage buildDenseTargetImage(const QImage &cameraSizedGrayImage) {
    DirectDenseTargetImage denseTarget;
    if (cameraSizedGrayImage.isNull()) {
        return denseTarget;
    }

    const QImage gray = cameraSizedGrayImage.convertToFormat(QImage::Format_Grayscale8);
    denseTarget.width = gray.width();
    denseTarget.height = gray.height();
    denseTarget.amplitude.resize(denseTarget.width * denseTarget.height);

    for (int y = 0; y < denseTarget.height; ++y) {
        const uchar *row = gray.constScanLine(y);
        const int rowBase = y * denseTarget.width;
        for (int x = 0; x < denseTarget.width; ++x) {
            denseTarget.amplitude[rowBase + x] = static_cast<float>(row[x]) / 255.0f;
        }
    }

    return denseTarget;
}

QVector<DirectGsTargetPoint> buildPointTargets(const QVector<QPointF> &cameraCenteredPoints) {
    QVector<DirectGsTargetPoint> targets;
    targets.reserve(cameraCenteredPoints.size());

    for (const QPointF &point : cameraCenteredPoints) {
        DirectGsTargetPoint target;
        target.xCamPx = point.x();
        target.yCamPx = point.y();
        targets.append(target);
    }

    return targets;
}

DirectGsResult runGerchbergSaxton(const DirectGsVariables &variables,
                                  const QVector<float> &sourceAmplitude,
                                  const QVector<DirectGsTargetPoint> &targets) {
    DirectGsResult result;
    PreparedGsData prepared;
    if (!preparePointTargetData(variables, sourceAmplitude, targets, result, prepared)) {
        return result;
    }
    return runPreparedGsCpu(variables, sourceAmplitude, prepared, result);
}

DirectGsResult runGerchbergSaxton(const DirectGsVariables &variables,
                                  const QVector<float> &sourceAmplitude,
                                  const DirectDenseTargetImage &denseTargetImage) {
    DirectGsResult result;
    PreparedGsData prepared;
    if (!prepareDenseTargetData(variables, sourceAmplitude, denseTargetImage, result, prepared)) {
        return result;
    }
    return runPreparedGsCpu(variables, sourceAmplitude, prepared, result);
}

DirectGsResult generatePhaseMaskFromImage(const QImage &image, const DirectGsVariables &variables) {
    DirectGsResult directResult;
    if (image.isNull()) {
        directResult.error = "Input image is empty.";
        return directResult;
    }

    bool usedDefaultSource = true;
    const QVector<float> sourceAmplitude = buildSourceAmplitude(variables, &usedDefaultSource);
    const QImage cameraSizedGrayImage = resizeImageToCameraGrayscale(image, variables);
    const DirectDenseTargetImage denseTarget = buildDenseTargetImage(cameraSizedGrayImage);

    directResult = runGerchbergSaxton(variables, sourceAmplitude, denseTarget);
    directResult.usedDefaultSource = usedDefaultSource;
    return directResult;
}

DirectGsResult generatePhaseMaskFromPoints(const QVector<QPointF> &cameraCenteredPoints,
                                           const DirectGsVariables &variables) {
    DirectGsResult directResult;
    if (cameraCenteredPoints.isEmpty()) {
        directResult.error = "No camera-centered target points were provided.";
        return directResult;
    }

    bool usedDefaultSource = true;
    const QVector<float> sourceAmplitude = buildSourceAmplitude(variables, &usedDefaultSource);
    const QVector<DirectGsTargetPoint> targets = buildPointTargets(cameraCenteredPoints);

    directResult = runGerchbergSaxton(variables, sourceAmplitude, targets);
    directResult.usedDefaultSource = usedDefaultSource;
    return directResult;
}

} // namespace AlgorithmDirect
