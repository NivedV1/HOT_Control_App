#ifndef DIRECT_RME_ALGORITHM_H
#define DIRECT_RME_ALGORITHM_H

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

namespace AlgorithmDirect {

struct DirectRmeVariables {
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
};

struct DirectRmeTargetPoint {
    // Camera-centered coordinates: (0, 0) is center, +x is right, +y is up.
    double xCamPx = 0.0;
    double yCamPx = 0.0;
};

struct DirectRmeResult {
    bool success = false;
    QString error;

    QImage phaseMask8Bit;
    QVector<double> wrappedPhaseRad;

    int requestedTargetCount = 0;
    int usedTargetCount = 0;
    int skippedOutsideCameraFov = 0;
    int skippedOutsideSlmBounds = 0;
};

QVector<DirectRmeTargetPoint> buildRmePointTargets(const QVector<QPointF> &cameraCenteredPoints);
DirectRmeResult generateRandomMaskEncodingPhaseMask(const QVector<QPointF> &cameraCenteredPoints,
                                                    const DirectRmeVariables &variables);
DirectRmeResult runRandomMaskEncoding(const DirectRmeVariables &variables,
                                      const QVector<DirectRmeTargetPoint> &targets);

} // namespace AlgorithmDirect

#endif // DIRECT_RME_ALGORITHM_H
