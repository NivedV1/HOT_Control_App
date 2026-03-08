#include "settingsdialog.h"
#include "components/arrowspinbox.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>

SettingsDialog::SettingsDialog(int slmW, int slmH, double slmPix, int backend, 
                               int camW, int camH, double camPix, 
                               double wave, double focal, QWidget *parent)
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
    pixelSpin->setSuffix(" µm"); pixelSpin->setValue(slmPix);
    
    slmForm->addRow("SLM Width (pixels):", widthSpin);
    slmForm->addRow("SLM Height (pixels):", heightSpin);
    slmForm->addRow("SLM Pixel Size:", pixelSpin);
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
    camPixelSpin->setSuffix(" µm"); camPixelSpin->setValue(camPix);
    
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

    // Assemble the dialog
    mainLayout->addWidget(slmGroup);
    mainLayout->addWidget(camGroup);
    mainLayout->addWidget(opticsGroup);
    
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
