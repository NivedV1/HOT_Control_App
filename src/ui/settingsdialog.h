#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox> // NEW

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int currentWidth, int currentHeight, double currentPixelSize, int currentBackend, QWidget *parent = nullptr);

    int getWidth() const;
    int getHeight() const;
    double getPixelSize() const;
    int getCameraBackend() const; // NEW

private:
    QSpinBox *widthSpin;
    QSpinBox *heightSpin;
    QDoubleSpinBox *pixelSpin;
    QComboBox *cameraBackendCombo; // NEW
};

#endif // SETTINGSDIALOG_H