#ifndef RME_ALGORITHM_H
#define RME_ALGORITHM_H

#include <QImage>
#include <QString>
#include <QVector>

namespace RMEAlgorithm {

struct RMETargetPoint {
    // Camera-centered coordinates: (0, 0) is center, +x is right, +y is up.
    double xCamPx = 0.0;
    double yCamPx = 0.0;
};

struct RMEConfig {
    int slmWidth = 0;
    int slmHeight = 0;
    double slmPixelSizeUm = 0.0;

    int camWidth = 0;
    int camHeight = 0;
    double camPixelSizeUm = 0.0;
    double cameraImagingMagnification = 1.0;

    double wavelengthNm = 0.0;
    double focalLengthMm = 0.0;
};

struct RMEResult {
    bool success = false;
    QString error;

    QImage phaseMask8Bit;
    QVector<double> wrappedPhaseRad;

    int requestedTargetCount = 0;
    int usedTargetCount = 0;
    int skippedOutsideCameraFov = 0;
    int skippedOutsideSlmBounds = 0;
};

RMEResult runRandomMaskEncoding(const RMEConfig &config,
                                const QVector<RMETargetPoint> &targets);

} // namespace RMEAlgorithm

#endif // RME_ALGORITHM_H
