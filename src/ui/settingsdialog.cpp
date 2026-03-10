#include "settingsdialog.h"
#include "components/arrowspinbox.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(int slmW, int slmH, double slmPix, int backend,
                               int camW, int camH, double camPix,
                               double wave, double focal, int slmOutputMode,
                               bool autoRunGsEnabled,
                               QWidget *parent)
    : QDialog(parent) {

    setWindowTitle("Hardware Settings");
    setMinimumWidth(400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // --- 1. SLM SETTINGS ---
    QGroupBox *slmGroup = new QGroupBox("SLM Parameters");
    QFormLayout *slmForm = new QFormLayout();

    widthSpin = new ArrowSpinBox(this); widthSpin->setRange(100, 8000); widthSpin->setValue(slmW);
    heightSpin = new ArrowSpinBox(this); heightSpin->setRange(100, 8000); heightSpin->setValue(slmH);
    pixelSpin = new ArrowDoubleSpinBox(this); pixelSpin->setRange(0.1, 100.0); pixelSpin->setDecimals(2);
    pixelSpin->setSuffix(" um"); pixelSpin->setValue(slmPix);

    slmOutputModeCombo = new QComboBox(this);
    slmOutputModeCombo->addItems({"DLL", "Direct Screen"});
    slmOutputModeCombo->setCurrentIndex((slmOutputMode == 1) ? 1 : 0);

    autoRunGsCheck = new QCheckBox(this);
    autoRunGsCheck->setChecked(autoRunGsEnabled);

    slmForm->addRow("SLM Width (pixels):", widthSpin);
    slmForm->addRow("SLM Height (pixels):", heightSpin);
    slmForm->addRow("SLM Pixel Size:", pixelSpin);
    slmForm->addRow("SLM Output Mode:", slmOutputModeCombo);
    slmForm->addRow("Auto-run GS:", autoRunGsCheck);
    slmGroup->setLayout(slmForm);

    // --- 2. CAMERA SETTINGS ---
    QGroupBox *camGroup = new QGroupBox("Camera Parameters");
    QFormLayout *camForm = new QFormLayout();

    cameraBackendCombo = new QComboBox(this);
    cameraBackendCombo->addItems({"Qt Native (WMF)", "OpenCV (DirectShow)"});
    cameraBackendCombo->setCurrentIndex(backend);

    camWidthSpin = new ArrowSpinBox(this); camWidthSpin->setRange(100, 8000); camWidthSpin->setValue(camW);
    camHeightSpin = new ArrowSpinBox(this); camHeightSpin->setRange(100, 8000); camHeightSpin->setValue(camH);
    camPixelSpin = new ArrowDoubleSpinBox(this); camPixelSpin->setRange(0.1, 100.0); camPixelSpin->setDecimals(2);
    camPixelSpin->setSuffix(" um"); camPixelSpin->setValue(camPix);

    camForm->addRow("Camera Engine:", cameraBackendCombo);
    camForm->addRow("Camera Width (px):", camWidthSpin);
    camForm->addRow("Camera Height (px):", camHeightSpin);
    camForm->addRow("Camera Pixel Size:", camPixelSpin);
    camGroup->setLayout(camForm);

    // --- 3. OPTICAL SETUP ---
    QGroupBox *opticsGroup = new QGroupBox("Optical Setup");
    QFormLayout *opticsForm = new QFormLayout();

    waveSpin = new ArrowDoubleSpinBox(this); waveSpin->setRange(100.0, 2000.0); waveSpin->setDecimals(1);
    waveSpin->setSuffix(" nm"); waveSpin->setValue(wave);

    focalSpin = new ArrowDoubleSpinBox(this); focalSpin->setRange(1.0, 1000.0); focalSpin->setDecimals(1);
    focalSpin->setSuffix(" mm"); focalSpin->setValue(focal);

    opticsForm->addRow("Laser Wavelength:", waveSpin);
    opticsForm->addRow("Fourier Lens Focal Length:", focalSpin);
    opticsGroup->setLayout(opticsForm);

    QLabel *monitorHint = new QLabel("Monitor target is selected from Tools > Select Monitor.");
    monitorHint->setWordWrap(true);

    // Assemble the dialog
    mainLayout->addWidget(slmGroup);
    mainLayout->addWidget(camGroup);
    mainLayout->addWidget(opticsGroup);
    mainLayout->addWidget(monitorHint);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

// SLM Getters
int SettingsDialog::getWidth() const { return widthSpin->value(); }
int SettingsDialog::getHeight() const { return heightSpin->value(); }
double SettingsDialog::getPixelSize() const { return pixelSpin->value(); }

// Camera Getters
int SettingsDialog::getCameraBackend() const { return cameraBackendCombo->currentIndex(); }
int SettingsDialog::getCamWidth() const { return camWidthSpin->value(); }
int SettingsDialog::getCamHeight() const { return camHeightSpin->value(); }
double SettingsDialog::getCamPixelSize() const { return camPixelSpin->value(); }

// Optical Getters
double SettingsDialog::getWavelength() const { return waveSpin->value(); }
double SettingsDialog::getFocalLength() const { return focalSpin->value(); }

// SLM Output Getter
int SettingsDialog::getSlmOutputMode() const { return slmOutputModeCombo->currentIndex(); }

// Algorithm behavior getters
bool SettingsDialog::getAutoRunGsEnabled() const { return autoRunGsCheck->isChecked(); }
