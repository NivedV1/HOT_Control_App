#include "gs_algorithm.h"

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>

#if HOT_ENABLE_CUDA_GS
#include "gs_algorithm_cuda.h"
#endif

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

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

cv::UMat roll2d(const cv::UMat &input, int shiftY, int shiftX) {
    if (input.empty()) {
        return cv::UMat();
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

    cv::UMat shiftedRows(input.size(), input.type());
    if (y == 0) {
        input.copyTo(shiftedRows);
    } else {
        input.rowRange(rows - y, rows).copyTo(shiftedRows.rowRange(0, y));
        input.rowRange(0, rows - y).copyTo(shiftedRows.rowRange(y, rows));
    }

    cv::UMat shifted(input.size(), input.type());
    if (x == 0) {
        shiftedRows.copyTo(shifted);
    } else {
        shiftedRows.colRange(cols - x, cols).copyTo(shifted.colRange(0, x));
        shiftedRows.colRange(0, cols - x).copyTo(shifted.colRange(x, cols));
    }

    return shifted;
}

cv::UMat fftshift2d(const cv::UMat &input) {
    return roll2d(input, input.rows / 2, input.cols / 2);
}

cv::UMat ifftshift2d(const cv::UMat &input) {
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

bool prepareGsData(const GSAlgorithm::GSConfig &config,
                   const QVector<float> &sourceAmplitude,
                   const QVector<GSAlgorithm::GSTargetPoint> &targets,
                   GSAlgorithm::GSResult &result,
                   PreparedGsData &prepared) {
    result = GSAlgorithm::GSResult();
    result.requestedTargetCount = targets.size();

    if (config.slmWidth <= 0 || config.slmHeight <= 0) {
        result.error = "Invalid SLM resolution for GS algorithm.";
        return false;
    }
    if (config.camWidth <= 0 || config.camHeight <= 0) {
        result.error = "Invalid camera resolution for GS algorithm.";
        return false;
    }
    if (config.iterations <= 0) {
        result.error = "Iterations must be greater than zero.";
        return false;
    }

    if (config.slmPixelSizeUm <= 0.0 || config.camPixelSizeUm <= 0.0 ||
        config.wavelengthNm <= 0.0 || config.focalLengthMm <= 0.0) {
        result.error = "Optical/hardware settings must be positive before running GS.";
        return false;
    }

    const int pixelCount = config.slmWidth * config.slmHeight;
    if (sourceAmplitude.size() != pixelCount) {
        result.error = "Source amplitude map size does not match SLM resolution.";
        return false;
    }

    if (targets.isEmpty()) {
        result.error = "No target points found. Add at least one point on the target grid.";
        return false;
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
        return false;
    }

    prepared.targetAmplitude.fill(0.0f, pixelCount);

    const int cx = config.slmWidth / 2;
    const int cy = config.slmHeight / 2;

    for (const GSAlgorithm::GSTargetPoint &target : targets) {
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
        if (prepared.targetAmplitude[idx] == 0.0f) {
            ++result.usedTargetCount;
        }
        prepared.targetAmplitude[idx] = 1.0f;
    }

    if (result.usedTargetCount == 0) {
        result.error = "No valid target points remained after camera/Fourier mapping.";
        return false;
    }

    prepared.initialPhaseRad.resize(pixelCount);
    switch (config.startingPhaseMask) {
    case GSAlgorithm::GSStartingPhaseMask::RandomPhase: {
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> phaseDist(-kPiF, kPiF);
        for (int idx = 0; idx < pixelCount; ++idx) {
            prepared.initialPhaseRad[idx] = phaseDist(rng);
        }
        break;
    }
    case GSAlgorithm::GSStartingPhaseMask::BinaryGrating:
        for (int y = 0; y < config.slmHeight; ++y) {
            const int rowBase = y * config.slmWidth;
            for (int x = 0; x < config.slmWidth; ++x) {
                const int idx = rowBase + x;
                prepared.initialPhaseRad[idx] = (x % 2 == 0) ? 0.0f : kPiF;
            }
        }
        break;
    case GSAlgorithm::GSStartingPhaseMask::Checkerboard:
    default:
        for (int y = 0; y < config.slmHeight; ++y) {
            const int rowBase = y * config.slmWidth;
            for (int x = 0; x < config.slmWidth; ++x) {
                const int idx = rowBase + x;
                prepared.initialPhaseRad[idx] = ((x + y) % 2 == 0) ? 0.0f : kPiF;
            }
        }
        break;
    }

    return true;
}

void populateResultPhaseImage(const QVector<float> &phaseRad,
                              int width,
                              int height,
                              GSAlgorithm::GSResult &result) {
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
            const int gray = std::clamp(static_cast<int>(scaled), 0, 255);
            row[x] = static_cast<uchar>(gray);
        }
    }
}

GSAlgorithm::GSResult runGerchbergSaxtonCpuInternal(const GSAlgorithm::GSConfig &config,
                                                    const QVector<float> &sourceAmplitude,
                                                    const PreparedGsData &prepared,
                                                    const GSAlgorithm::GSResult &baseResult) {
    GSAlgorithm::GSResult result = baseResult;
    const int pixelCount = config.slmWidth * config.slmHeight;

    QVector<double> slmPhase(pixelCount, 0.0);
    for (int i = 0; i < pixelCount; ++i) {
        slmPhase[i] = static_cast<double>(prepared.initialPhaseRad[i]);
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
                const double amp = static_cast<double>(prepared.targetAmplitude[idx]);
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

    QVector<float> outputPhase(pixelCount, 0.0f);
    for (int i = 0; i < pixelCount; ++i) {
        outputPhase[i] = static_cast<float>(slmPhase[i]);
    }
    populateResultPhaseImage(outputPhase, config.slmWidth, config.slmHeight, result);

    result.success = true;
    result.backendUsed = GSAlgorithm::GSComputeBackendUsed::CPU;
    if (result.backendInfo.isEmpty()) {
        result.backendInfo = "CPU (OpenCV DFT)";
    }
    return result;
}

#if HOT_ENABLE_OPENCL_GS
struct OclDeviceRecord {
    int platformIndex = -1;
    int deviceIndex = -1;
    QString platformName;
    cv::ocl::Device device;
    bool isGpu = false;
};

QVector<OclDeviceRecord> enumerateOclDeviceRecords() {
    QVector<OclDeviceRecord> out;
    try {
        if (!cv::ocl::haveOpenCL()) {
            return out;
        }

        std::vector<cv::ocl::PlatformInfo> platforms;
        cv::ocl::getPlatfomsInfo(platforms);

        for (int p = 0; p < static_cast<int>(platforms.size()); ++p) {
            const int count = platforms[p].deviceNumber();
            for (int d = 0; d < count; ++d) {
                cv::ocl::Device dev;
                platforms[p].getDevice(dev, d);
                if (dev.empty()) {
                    continue;
                }

                OclDeviceRecord rec;
                rec.platformIndex = p;
                rec.deviceIndex = d;
                rec.platformName = QString::fromStdString(platforms[p].name());
                rec.device = dev;
                rec.isGpu = (dev.type() & cv::ocl::Device::TYPE_GPU) != 0;
                out.append(rec);
            }
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

bool selectOpenClDevice(const GSAlgorithm::GSConfig &config,
                        OclDeviceRecord &selected,
                        QString &error) {
    const QVector<OclDeviceRecord> records = enumerateOclDeviceRecords();
    if (records.isEmpty()) {
        error = "No OpenCL devices detected by OpenCV.";
        return false;
    }

    const bool explicitSelection = (config.openClPlatformIndex >= 0 && config.openClDeviceIndex >= 0);
    if (explicitSelection) {
        for (const OclDeviceRecord &rec : records) {
            if (rec.platformIndex == config.openClPlatformIndex &&
                rec.deviceIndex == config.openClDeviceIndex) {
                selected = rec;
                return true;
            }
        }

        error = QString("Selected OpenCL device P%1:D%2 is unavailable.")
                    .arg(config.openClPlatformIndex)
                    .arg(config.openClDeviceIndex);
        return false;
    }

    for (const OclDeviceRecord &rec : records) {
        if (rec.isGpu) {
            selected = rec;
            return true;
        }
    }

    selected = records.first();
    return true;
}

bool bindOpenCvOpenClDevice(const OclDeviceRecord &selected, QString &backendInfo, QString &error) {
    try {
        cv::ocl::setUseOpenCL(true);
        if (!cv::ocl::haveOpenCL()) {
            error = "OpenCV reports OpenCL is unavailable.";
            return false;
        }

        cv::ocl::Context ctx = cv::ocl::Context::fromDevice(selected.device);
        if (ctx.empty()) {
            error = "Failed to create OpenCL context from selected device.";
            return false;
        }

        cv::ocl::OpenCLExecutionContext execCtx = cv::ocl::OpenCLExecutionContext::create(ctx, selected.device);
        if (!execCtx.useOpenCL()) {
            execCtx.setUseOpenCL(true);
        }
        execCtx.bind();
        cv::ocl::setUseOpenCL(true);

        if (!cv::ocl::useOpenCL()) {
            error = "Failed to activate OpenCL execution context in OpenCV.";
            return false;
        }

        const QString kind = selected.isGpu ? "GPU" : "CPU/Other";
        backendInfo = QString("OpenCL (OpenCV UMat) P%1:D%2 - %3 / %4 (%5)")
                          .arg(selected.platformIndex)
                          .arg(selected.deviceIndex)
                          .arg(selected.platformName)
                          .arg(QString::fromStdString(selected.device.name()))
                          .arg(kind);
        return true;
    } catch (const cv::Exception &e) {
        error = QString("OpenCV OpenCL bind failed: %1").arg(QString::fromStdString(e.what()));
        return false;
    } catch (const std::exception &e) {
        error = QString("OpenCL bind failed: %1").arg(e.what());
        return false;
    } catch (...) {
        error = "OpenCL bind failed with unknown exception.";
        return false;
    }
}

bool bindOpenCvDefaultOpenClDevice(QString &backendInfo, QString &error) {
    try {
        cv::ocl::setUseOpenCL(true);
        if (!cv::ocl::haveOpenCL()) {
            error = "OpenCV reports OpenCL is unavailable.";
            return false;
        }

        cv::ocl::Context &ctx = cv::ocl::Context::getDefault(true);
        if (ctx.empty() || ctx.ndevices() == 0) {
            error = "OpenCV default OpenCL context has no devices.";
            return false;
        }

        const cv::ocl::Device &dev = cv::ocl::Device::getDefault();
        if (dev.empty()) {
            error = "OpenCV default OpenCL device is empty.";
            return false;
        }

        if (!cv::ocl::useOpenCL()) {
            cv::ocl::setUseOpenCL(true);
        }
        if (!cv::ocl::useOpenCL()) {
            error = "Failed to activate OpenCL on OpenCV default device.";
            return false;
        }

        const bool isGpu = (dev.type() & cv::ocl::Device::TYPE_GPU) != 0;
        const QString kind = isGpu ? "GPU" : "CPU/Other";
        backendInfo = QString("OpenCL (OpenCV default) %1 / %2 (%3)")
                          .arg(QString::fromStdString(dev.vendorName()))
                          .arg(QString::fromStdString(dev.name()))
                          .arg(kind);
        return true;
    } catch (const cv::Exception &e) {
        error = QString("OpenCV default OpenCL bind failed: %1").arg(QString::fromStdString(e.what()));
        return false;
    } catch (const std::exception &e) {
        error = QString("Default OpenCL bind failed: %1").arg(e.what());
        return false;
    } catch (...) {
        error = "Default OpenCL bind failed with unknown exception.";
        return false;
    }
}

GSAlgorithm::GSResult runGerchbergSaxtonOpenClInternal(const GSAlgorithm::GSConfig &config,
                                                       const QVector<float> &sourceAmplitude,
                                                       const PreparedGsData &prepared,
                                                       const GSAlgorithm::GSResult &baseResult) {
    GSAlgorithm::GSResult result = baseResult;
    result.backendUsed = GSAlgorithm::GSComputeBackendUsed::OpenCL;
    try {
        OclDeviceRecord selected;
        QString error;
        const bool haveSelected = selectOpenClDevice(config, selected, error);
        if (haveSelected) {
            if (!bindOpenCvOpenClDevice(selected, result.backendInfo, error)) {
                QString defaultError;
                if (!bindOpenCvDefaultOpenClDevice(result.backendInfo, defaultError)) {
                    result.error = QString("%1 | %2").arg(error, defaultError);
                    return result;
                }
                result.backendInfo += " (selected device bind failed; using OpenCV default device)";
            }
        } else {
            QString defaultError;
            if (!bindOpenCvDefaultOpenClDevice(result.backendInfo, defaultError)) {
                result.error = QString("%1 | %2").arg(error, defaultError);
                return result;
            }
            result.backendInfo += " (selected OpenCL device unavailable; using OpenCV default device)";
        }

        const int width = config.slmWidth;
        const int height = config.slmHeight;
        const int pixelCount = width * height;

        cv::Mat sourceAmpHost(height, width, CV_32F, const_cast<float *>(sourceAmplitude.constData()));
        cv::Mat targetAmpHost(height, width, CV_32F, const_cast<float *>(prepared.targetAmplitude.constData()));
        cv::Mat phaseHost(height, width, CV_32F, const_cast<float *>(prepared.initialPhaseRad.constData()));

        cv::UMat sourceAmpU;
        cv::UMat targetAmpU;
        cv::UMat phaseU;
        sourceAmpHost.copyTo(sourceAmpU);
        targetAmpHost.copyTo(targetAmpU);
        phaseHost.copyTo(phaseU);

        cv::UMat realU;
        cv::UMat imagU;
        cv::UMat slmFieldU;
        cv::UMat focalFieldU;
        cv::UMat focalPhaseU;
        cv::UMat focalModifiedU;
        cv::UMat slmFieldNewU;

        std::vector<cv::UMat> channels(2);

        for (int iter = 0; iter < config.iterations; ++iter) {
            cv::polarToCart(sourceAmpU, phaseU, realU, imagU, false);
            channels[0] = realU;
            channels[1] = imagU;
            cv::merge(channels, slmFieldU);

            cv::dft(ifftshift2d(slmFieldU), focalFieldU, cv::DFT_COMPLEX_OUTPUT);
            focalFieldU = fftshift2d(focalFieldU);

            cv::split(focalFieldU, channels);
            cv::phase(channels[0], channels[1], focalPhaseU, false);
            cv::polarToCart(targetAmpU, focalPhaseU, realU, imagU, false);
            channels[0] = realU;
            channels[1] = imagU;
            cv::merge(channels, focalModifiedU);

            cv::dft(ifftshift2d(focalModifiedU),
                    slmFieldNewU,
                    cv::DFT_INVERSE | cv::DFT_COMPLEX_OUTPUT | cv::DFT_SCALE);
            slmFieldNewU = fftshift2d(slmFieldNewU);

            cv::split(slmFieldNewU, channels);
            cv::phase(channels[0], channels[1], phaseU, false);
        }

        cv::Mat finalPhaseHost;
        phaseU.copyTo(finalPhaseHost);
        if (finalPhaseHost.empty() || finalPhaseHost.rows != height || finalPhaseHost.cols != width) {
            result.error = "OpenCV OpenCL GS returned invalid phase data.";
            return result;
        }

        QVector<float> phaseOut(pixelCount, 0.0f);
        for (int y = 0; y < height; ++y) {
            const float *row = finalPhaseHost.ptr<float>(y);
            const int rowBase = y * width;
            for (int x = 0; x < width; ++x) {
                phaseOut[rowBase + x] = row[x];
            }
        }

        populateResultPhaseImage(phaseOut, width, height, result);
        result.success = true;
        return result;
    } catch (const cv::Exception &e) {
        result.error = QString("OpenCV OpenCL GS failed: %1").arg(QString::fromStdString(e.what()));
        return result;
    } catch (const std::exception &e) {
        result.error = QString("OpenCL GS failed: %1").arg(e.what());
        return result;
    } catch (...) {
        result.error = "OpenCL GS failed with unknown exception.";
        return result;
    }
}
#endif
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

QVector<GSOpenClDeviceInfo> enumerateOpenClDevices() {
    QVector<GSOpenClDeviceInfo> devices;
#if HOT_ENABLE_OPENCL_GS
    const QVector<OclDeviceRecord> records = enumerateOclDeviceRecords();
    devices.reserve(records.size());

    for (const OclDeviceRecord &rec : records) {
        GSOpenClDeviceInfo info;
        info.platformIndex = rec.platformIndex;
        info.deviceIndex = rec.deviceIndex;
        info.isGpu = rec.isGpu;
        const QString kind = rec.isGpu ? "GPU" : "CPU/Other";
        info.displayName = QString("[P%1:D%2] %3 / %4 (%5)")
                               .arg(rec.platformIndex)
                               .arg(rec.deviceIndex)
                               .arg(rec.platformName)
                               .arg(QString::fromStdString(rec.device.name()))
                               .arg(kind);
        devices.append(info);
    }
#endif
    return devices;
}

QVector<GSCudaDeviceInfo> enumerateCudaDevices() {
    QVector<GSCudaDeviceInfo> devices;
#if HOT_ENABLE_CUDA_GS
    devices = CudaBackend::enumerateCudaDevicesNative();
#endif
    return devices;
}

GSResult runGerchbergSaxton(const GSConfig &config,
                            const QVector<float> &sourceAmplitude,
                            const QVector<GSTargetPoint> &targets) {
    try {
        GSResult preparedResult;
        PreparedGsData prepared;
        if (!prepareGsData(config, sourceAmplitude, targets, preparedResult, prepared)) {
            preparedResult.backendUsed = GSComputeBackendUsed::CPU;
            if (preparedResult.backendInfo.isEmpty()) {
                preparedResult.backendInfo = "CPU (OpenCV DFT)";
            }
            return preparedResult;
        }

        if (config.computeBackend == GSComputeBackend::CPU) {
            return runGerchbergSaxtonCpuInternal(config, sourceAmplitude, prepared, preparedResult);
        }

        bool cudaTried = false;
        QString cudaFailure;

        if (config.computeBackend == GSComputeBackend::Auto || config.computeBackend == GSComputeBackend::CUDA) {
            cudaTried = true;
#if HOT_ENABLE_CUDA_GS
            GSResult cudaResult = CudaBackend::runGerchbergSaxtonCudaNative(config,
                                                                             sourceAmplitude,
                                                                             prepared.targetAmplitude,
                                                                             prepared.initialPhaseRad,
                                                                             preparedResult);
            if (cudaResult.success) {
                return cudaResult;
            }
            cudaFailure = cudaResult.error;
#else
            cudaFailure = "CUDA GS backend is disabled in this build.";
#endif

            if (config.computeBackend == GSComputeBackend::CUDA) {
                GSResult cpuResult = runGerchbergSaxtonCpuInternal(config, sourceAmplitude, prepared, preparedResult);
                cpuResult.fallbackOccurred = true;
                cpuResult.fallbackReason = cudaFailure;
                return cpuResult;
            }
        }

        bool openClTried = false;
        QString openClFailure;
#if HOT_ENABLE_OPENCL_GS
        if (config.computeBackend == GSComputeBackend::Auto || config.computeBackend == GSComputeBackend::OpenCL) {
            openClTried = true;
            GSResult openClResult = runGerchbergSaxtonOpenClInternal(config, sourceAmplitude, prepared, preparedResult);
            if (openClResult.success) {
                if (config.computeBackend == GSComputeBackend::Auto && cudaTried && !cudaFailure.isEmpty()) {
                    openClResult.fallbackOccurred = true;
                    openClResult.fallbackReason = QString("CUDA: %1").arg(cudaFailure);
                }
                return openClResult;
            }
            openClFailure = openClResult.error;
        }
#else
        if (config.computeBackend == GSComputeBackend::Auto || config.computeBackend == GSComputeBackend::OpenCL) {
            openClTried = true;
            openClFailure = "OpenCL GS backend is disabled in this build.";
        }
#endif

        GSResult cpuResult = runGerchbergSaxtonCpuInternal(config, sourceAmplitude, prepared, preparedResult);
        if (cudaTried || openClTried) {
            cpuResult.fallbackOccurred = true;
            QString fallbackReason;
            if (cudaTried && !cudaFailure.isEmpty()) {
                fallbackReason += QString("CUDA: %1").arg(cudaFailure);
            }
            if (openClTried && !openClFailure.isEmpty()) {
                if (!fallbackReason.isEmpty()) {
                    fallbackReason += " | ";
                }
                fallbackReason += QString("OpenCL: %1").arg(openClFailure);
            }
            cpuResult.fallbackReason = fallbackReason;
        }
        return cpuResult;
    } catch (const cv::Exception &e) {
        GSResult fail;
        fail.success = false;
        fail.backendUsed = GSComputeBackendUsed::CPU;
        fail.backendInfo = "CPU (OpenCV DFT)";
        fail.error = QString("GS execution failed with OpenCV exception: %1").arg(QString::fromStdString(e.what()));
        return fail;
    } catch (const std::exception &e) {
        GSResult fail;
        fail.success = false;
        fail.backendUsed = GSComputeBackendUsed::CPU;
        fail.backendInfo = "CPU (OpenCV DFT)";
        fail.error = QString("GS execution failed with exception: %1").arg(e.what());
        return fail;
    } catch (...) {
        GSResult fail;
        fail.success = false;
        fail.backendUsed = GSComputeBackendUsed::CPU;
        fail.backendInfo = "CPU (OpenCV DFT)";
        fail.error = "GS execution failed with unknown exception.";
        return fail;
    }
}

} // namespace GSAlgorithm
