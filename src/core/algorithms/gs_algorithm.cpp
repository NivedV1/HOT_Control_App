#include "gs_algorithm.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

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

} // namespace

namespace GSAlgorithm {

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
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const double r2 = dx * dx + dy * dy;
            const double amplitude = std::exp(-r2 / denom);
            source[y * width + x] = static_cast<float>(amplitude);
        }
    }

    return source;
}

GSResult runGerchbergSaxton(const GSConfig &config,
                            const QVector<float> &sourceAmplitude,
                            const QVector<GSTargetPoint> &targets) {
    GSResult result;
    result.requestedTargetCount = targets.size();

    if (config.slmWidth <= 0 || config.slmHeight <= 0) {
        result.error = "Invalid SLM resolution for GS algorithm.";
        return result;
    }
    if (config.camWidth <= 0 || config.camHeight <= 0) {
        result.error = "Invalid camera resolution for GS algorithm.";
        return result;
    }
    if (config.iterations <= 0) {
        result.error = "Iterations must be greater than zero.";
        return result;
    }

    if (config.slmPixelSizeUm <= 0.0 || config.camPixelSizeUm <= 0.0 ||
        config.wavelengthNm <= 0.0 || config.focalLengthMm <= 0.0) {
        result.error = "Optical/hardware settings must be positive before running GS.";
        return result;
    }

    const int pixelCount = config.slmWidth * config.slmHeight;
    if (sourceAmplitude.size() != pixelCount) {
        result.error = "Source amplitude map size does not match SLM resolution.";
        return result;
    }

    if (targets.isEmpty()) {
        result.error = "No target points found. Add at least one point on the target grid.";
        return result;
    }

    const double slmDx = config.slmPixelSizeUm * 1e-6;
    const double slmDy = config.slmPixelSizeUm * 1e-6;
    const double camDx = config.camPixelSizeUm * 1e-6;
    const double camDy = config.camPixelSizeUm * 1e-6;
    const double wavelength = config.wavelengthNm * 1e-9;
    const double focalLength = config.focalLengthMm * 1e-3;

    const double focalDx = (wavelength * focalLength) / (static_cast<double>(config.slmWidth) * slmDx);
    const double focalDy = (wavelength * focalLength) / (static_cast<double>(config.slmHeight) * slmDy);

    if (focalDx == 0.0 || focalDy == 0.0) {
        result.error = "Computed focal-plane pixel pitch is zero. Check optical settings.";
        return result;
    }

    QVector<double> targetAmplitude(pixelCount, 0.0);
    const int cx = config.slmWidth / 2;
    const int cy = config.slmHeight / 2;

    for (const GSTargetPoint &target : targets) {
        if (std::abs(target.xCamPx) > static_cast<double>(config.camWidth) / 2.0 ||
            std::abs(target.yCamPx) > static_cast<double>(config.camHeight) / 2.0) {
            ++result.skippedOutsideCameraFov;
            continue;
        }

        const double physX = target.xCamPx * camDx;
        const double physY = target.yCamPx * camDy;

        const int fftOffsetX = static_cast<int>(std::llround(physX / focalDx));
        const int fftOffsetY = static_cast<int>(std::llround(physY / focalDy));

        const int fftX = cx + fftOffsetX;
        const int fftY = cy - fftOffsetY;

        if (fftX < 0 || fftX >= config.slmWidth || fftY < 0 || fftY >= config.slmHeight) {
            ++result.skippedOutsideSlmBounds;
            continue;
        }

        const int idx = fftY * config.slmWidth + fftX;
        if (targetAmplitude[idx] == 0.0) {
            ++result.usedTargetCount;
        }
        targetAmplitude[idx] = 1.0;
    }

    if (result.usedTargetCount == 0) {
        result.error = "No valid target points remained after camera/Fourier mapping.";
        return result;
    }

    QVector<double> slmPhase(pixelCount, 0.0);
    std::mt19937_64 rng(std::random_device{}());
    std::uniform_real_distribution<double> phaseDist(-kPi, kPi);
    for (int i = 0; i < pixelCount; ++i) {
        slmPhase[i] = phaseDist(rng);
    }

    cv::Mat slmField(config.slmHeight, config.slmWidth, CV_64FC2);
    cv::Mat focalField;
    cv::Mat focalModified(config.slmHeight, config.slmWidth, CV_64FC2);
    cv::Mat slmFieldNew;

    for (int iter = 0; iter < config.iterations; ++iter) {
        for (int y = 0; y < config.slmHeight; ++y) {
            cv::Vec2d *row = slmField.ptr<cv::Vec2d>(y);
            const int rowBase = y * config.slmWidth;
            for (int x = 0; x < config.slmWidth; ++x) {
                const int idx = rowBase + x;
                const double amp = static_cast<double>(sourceAmplitude[idx]);
                row[x][0] = amp * std::cos(slmPhase[idx]);
                row[x][1] = amp * std::sin(slmPhase[idx]);
            }
        }

        cv::dft(ifftshift2d(slmField), focalField, cv::DFT_COMPLEX_OUTPUT);
        focalField = fftshift2d(focalField);

        for (int y = 0; y < config.slmHeight; ++y) {
            const cv::Vec2d *srcRow = focalField.ptr<cv::Vec2d>(y);
            cv::Vec2d *dstRow = focalModified.ptr<cv::Vec2d>(y);
            const int rowBase = y * config.slmWidth;
            for (int x = 0; x < config.slmWidth; ++x) {
                const int idx = rowBase + x;
                const double phase = std::atan2(srcRow[x][1], srcRow[x][0]);
                const double amp = targetAmplitude[idx];
                dstRow[x][0] = amp * std::cos(phase);
                dstRow[x][1] = amp * std::sin(phase);
            }
        }

        cv::dft(ifftshift2d(focalModified),
                slmFieldNew,
                cv::DFT_INVERSE | cv::DFT_COMPLEX_OUTPUT | cv::DFT_SCALE);
        slmFieldNew = fftshift2d(slmFieldNew);

        for (int y = 0; y < config.slmHeight; ++y) {
            const cv::Vec2d *row = slmFieldNew.ptr<cv::Vec2d>(y);
            const int rowBase = y * config.slmWidth;
            for (int x = 0; x < config.slmWidth; ++x) {
                const int idx = rowBase + x;
                slmPhase[idx] = std::atan2(row[x][1], row[x][0]);
            }
        }
    }

    result.wrappedPhaseRad.resize(pixelCount);
    result.phaseMask8Bit = QImage(config.slmWidth, config.slmHeight, QImage::Format_Grayscale8);

    for (int y = 0; y < config.slmHeight; ++y) {
        uchar *row = result.phaseMask8Bit.scanLine(y);
        const int rowBase = y * config.slmWidth;
        for (int x = 0; x < config.slmWidth; ++x) {
            const int idx = rowBase + x;

            double wrapped = std::fmod(slmPhase[idx], kTwoPi);
            if (wrapped < 0.0) {
                wrapped += kTwoPi;
            }

            result.wrappedPhaseRad[idx] = wrapped;

            const double scaled = (wrapped / kTwoPi) * 255.0;
            const int gray = std::clamp(static_cast<int>(scaled), 0, 255);
            row[x] = static_cast<uchar>(gray);
        }
    }

    result.success = true;
    return result;
}

} // namespace GSAlgorithm
