#ifndef GS_ALGORITHM_H
#define GS_ALGORITHM_H

#include <QImage>
#include <QString>
#include <QVector>

namespace GSAlgorithm {

enum class GSStartingPhaseMask {
    Checkerboard = 0,
    BinaryGrating = 1,
    RandomPhase = 2
};

enum class GSComputeBackend {
    Auto = 0,
    CPU = 1,
    OpenCL = 2,
    CUDA = 3
};

enum class GSComputeBackendUsed {
    CPU = 0,
    OpenCL = 1,
    CUDA = 2
};

struct GSTargetPoint {
    // Camera-centered coordinates: (0, 0) is center, +x is right, +y is up.
    double xCamPx = 0.0;
    double yCamPx = 0.0;
};

struct GSConfig {
    int slmWidth = 0;
    int slmHeight = 0;
    double slmPixelSizeUm = 0.0;

    int camWidth = 0;
    int camHeight = 0;
    double camPixelSizeUm = 0.0;

    double wavelengthNm = 0.0;
    double focalLengthMm = 0.0;

    int iterations = 20;
    GSStartingPhaseMask startingPhaseMask = GSStartingPhaseMask::Checkerboard;
    GSComputeBackend computeBackend = GSComputeBackend::Auto;
    int openClPlatformIndex = 0;
    int openClDeviceIndex = 0;
    int cudaDeviceIndex = 0;
};

struct GSResult {
    bool success = false;
    QString error;

    QImage phaseMask8Bit;
    QVector<double> wrappedPhaseRad;

    int requestedTargetCount = 0;
    int usedTargetCount = 0;
    int skippedOutsideCameraFov = 0;
    int skippedOutsideSlmBounds = 0;

    GSComputeBackendUsed backendUsed = GSComputeBackendUsed::CPU;
    QString backendInfo;
    bool fallbackOccurred = false;
    QString fallbackReason;
};

struct GSOpenClDeviceInfo {
    int platformIndex = -1;
    int deviceIndex = -1;
    QString displayName;
    bool isGpu = false;
};

struct GSCudaDeviceInfo {
    int deviceIndex = -1;
    QString displayName;
    bool isCompatible = false;
};

QVector<float> buildGaussianSourceAmplitude(int width, int height, double beamWaistPx);
QVector<GSOpenClDeviceInfo> enumerateOpenClDevices();
QVector<GSCudaDeviceInfo> enumerateCudaDevices();

GSResult runGerchbergSaxton(const GSConfig &config,
                            const QVector<float> &sourceAmplitude,
                            const QVector<GSTargetPoint> &targets);

} // namespace GSAlgorithm

#endif // GS_ALGORITHM_H

