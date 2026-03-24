#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QString>

class QCheckBox;
class QLineEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int slmW, int slmH, double slmPix, int backend,
                   int camW, int camH, double camPix,
                   const QString &udpBindIp, int udpPort,
                   double wave, double focal, double cameraImagingMagnification, int slmOutputMode,
                   bool autoRunGsEnabled,
                   bool autoSendSlmEnabled,
                   int startingPhaseMaskMode,
                   int gsComputeBackendMode,
                   int openClPlatformIndex,
                   int openClDeviceIndex,
                   int cudaDeviceIndex,
                   int cameraRotation,
                   bool flipX,
                   bool saveCompressed,
                   bool saveFollowsTransforms,
                   bool flipY,
                   QWidget *parent = nullptr);

    // SLM Getters
    int getWidth() const;
    int getHeight() const;
    double getPixelSize() const;

    // Camera Getters
    int getCameraBackend() const;
    int getCamWidth() const;
    int getCamHeight() const;
    double getCamPixelSize() const;
    QString getUdpBindIp() const;
    int getUdpPort() const;
    
    // UI Settings Getters
    int getCameraRotation() const;
    bool getFlipX() const;
    bool getFlipY() const;
    bool getSaveCompressed() const;
    bool getSaveFollowsTransforms() const;

    // Optical Getters
    double getWavelength() const;
    double getFocalLength() const;
    double getCameraImagingMagnification() const;

    // SLM Output Getters
    int getSlmOutputMode() const;

    // Algorithm behavior getters
    bool getAutoRunGsEnabled() const;
    bool getAutoSendSlmEnabled() const;
    int getStartingPhaseMaskMode() const;
    int getGsComputeBackendMode() const;
    int getOpenClPlatformIndex() const;
    int getOpenClDeviceIndex() const;
    int getCudaDeviceIndex() const;

signals:
    void applyRequested();

private:
    // SLM
    QSpinBox *widthSpin;
    QSpinBox *heightSpin;
    QDoubleSpinBox *pixelSpin;
    QComboBox *slmOutputModeCombo;
    QCheckBox *autoRunGsCheck;
    QCheckBox *autoSendSlmCheck;
    QComboBox *startingPhaseMaskCombo;
    QComboBox *computeBackendCombo;
    QComboBox *openClDeviceCombo;
    QComboBox *cudaDeviceCombo;

    // Camera
    QComboBox *cameraBackendCombo;
    QSpinBox *camWidthSpin;
    QSpinBox *camHeightSpin;
    QDoubleSpinBox *camPixelSpin;
    QLineEdit *udpBindIpEdit;
    QSpinBox *udpPortSpin;

    // UI Settings
    QComboBox *cameraRotationCombo;
    QCheckBox *flipXCheck;
    QCheckBox *flipYCheck;
    QCheckBox *saveCompressedCheck;
    QCheckBox *saveFollowsTransformsCheck;

    // Optics
    QDoubleSpinBox *waveSpin;
    QDoubleSpinBox *focalSpin;
    QDoubleSpinBox *cameraMagnificationSpin;
};

#endif // SETTINGSDIALOG_H
