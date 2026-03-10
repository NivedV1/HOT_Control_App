#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>

class QCheckBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int slmW, int slmH, double slmPix, int backend,
                   int camW, int camH, double camPix,
                   double wave, double focal, int slmOutputMode,
                   bool autoRunGsEnabled,
                   bool autoSendSlmEnabled,
                   int startingPhaseMaskMode,
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

    // Optical Getters
    double getWavelength() const;
    double getFocalLength() const;

    // SLM Output Getters
    int getSlmOutputMode() const;

    // Algorithm behavior getters
    bool getAutoRunGsEnabled() const;
    bool getAutoSendSlmEnabled() const;
    int getStartingPhaseMaskMode() const;

private:
    // SLM
    QSpinBox *widthSpin;
    QSpinBox *heightSpin;
    QDoubleSpinBox *pixelSpin;
    QComboBox *slmOutputModeCombo;
    QCheckBox *autoRunGsCheck;
    QCheckBox *autoSendSlmCheck;
    QComboBox *startingPhaseMaskCombo;

    // Camera
    QComboBox *cameraBackendCombo;
    QSpinBox *camWidthSpin;
    QSpinBox *camHeightSpin;
    QDoubleSpinBox *camPixelSpin;

    // Optics
    QDoubleSpinBox *waveSpin;
    QDoubleSpinBox *focalSpin;
};

#endif // SETTINGSDIALOG_H

