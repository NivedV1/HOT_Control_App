#ifndef DIRECT_GS_ALGORITHM_H
#define DIRECT_GS_ALGORITHM_H

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

namespace AlgorithmDirect {

enum class DirectGsStartingPhaseMask {
    Checkerboard = 0,
    BinaryGrating = 1,
    RandomPhase = 2
};

struct DirectGsVariables {
    // SLM hardware variables.
    int slmWidth = 1920;
    int slmHeight = 1080;
    double slmPixelSizeUm = 8.0;

    // Camera/trap-plane variables.
    int camWidth = 1920;
    int camHeight = 1080;
    double camPixelSizeUm = 5.0;
    double cameraImagingMagnification = 1.0;

    // Optical variables used for camera-pixel to Fourier-grid mapping.
    double wavelengthNm = 1064.0;
    double focalLengthMm = 100.0;

    // Gerchberg-Saxton variables.
    int iterations = 20;
    DirectGsStartingPhaseMask startingPhaseMask = DirectGsStartingPhaseMask::Checkerboard;

    // Source-amplitude variables.
    // If sourceAmplitude is empty or has the wrong size, a Gaussian source is generated
    // with beam waist min(slmWidth, slmHeight) / 6, matching the UI default.
    QVector<float> sourceAmplitude;
    double defaultSourceBeamWaistPx = 0.0;
};

struct DirectGsTargetPoint {
    // Camera-centered coordinates: (0, 0) is center, +x is right, +y is up.
    double xCamPx = 0.0;
    double yCamPx = 0.0;
};

struct DirectDenseTargetImage {
    // Camera-space amplitude map stored row-major. Values are expected in [0, 1].
    QVector<float> amplitude;
    int width = 0;
    int height = 0;
};

struct DirectGsResult {
    bool success = false;
    QString error;

    QImage phaseMask8Bit;
    QVector<double> wrappedPhaseRad;

    int requestedTargetCount = 0;
    int usedTargetCount = 0;
    int skippedOutsideCameraFov = 0;
    int skippedOutsideSlmBounds = 0;
    bool usedDefaultSource = true;
};

QVector<float> buildGaussianSourceAmplitude(int width, int height, double beamWaistPx);
QVector<float> buildSourceAmplitude(const DirectGsVariables &variables, bool *usedDefaultSource = nullptr);
QImage resizeImageToCameraGrayscale(const QImage &image, const DirectGsVariables &variables);
DirectDenseTargetImage buildDenseTargetImage(const QImage &cameraSizedGrayImage);
QVector<DirectGsTargetPoint> buildPointTargets(const QVector<QPointF> &cameraCenteredPoints);

DirectGsResult generatePhaseMaskFromImage(const QImage &image, const DirectGsVariables &variables);
DirectGsResult generatePhaseMaskFromPoints(const QVector<QPointF> &cameraCenteredPoints,
                                           const DirectGsVariables &variables);
DirectGsResult runGerchbergSaxton(const DirectGsVariables &variables,
                                   const QVector<float> &sourceAmplitude,
                                   const QVector<DirectGsTargetPoint> &targets);
DirectGsResult runGerchbergSaxton(const DirectGsVariables &variables,
                                   const QVector<float> &sourceAmplitude,
                                   const DirectDenseTargetImage &denseTargetImage);

} // namespace AlgorithmDirect

#endif // DIRECT_GS_ALGORITHM_H
