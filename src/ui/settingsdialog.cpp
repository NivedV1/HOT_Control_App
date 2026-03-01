#include "settingsdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>

SettingsDialog::SettingsDialog(int currentWidth, int currentHeight, double currentPixelSize, int currentBackend, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Hardware Settings");
    setMinimumWidth(350);
    
    QFormLayout *form = new QFormLayout(this);
    
    // SLM Settings
    widthSpin = new QSpinBox(this);
    widthSpin->setRange(100, 8000);
    widthSpin->setValue(currentWidth);
    
    heightSpin = new QSpinBox(this);
    heightSpin->setRange(100, 8000);
    heightSpin->setValue(currentHeight);
    
    pixelSpin = new QDoubleSpinBox(this);
    pixelSpin->setRange(0.1, 100.0);
    pixelSpin->setDecimals(2);
    pixelSpin->setSuffix(" µm");
    pixelSpin->setValue(currentPixelSize);
    
    form->addRow("SLM Width (pixels):", widthSpin);
    form->addRow("SLM Height (pixels):", heightSpin);
    form->addRow("SLM Pixel Size:", pixelSpin);
    
    // NEW: Camera Backend Settings
    cameraBackendCombo = new QComboBox(this);
    cameraBackendCombo->addItems({
        "Qt Native (WMF - Standard Webcams)", 
        "OpenCV (DirectShow - Best for OBS & Lab Cams)"
    });
    cameraBackendCombo->setCurrentIndex(currentBackend);
    form->addRow("Camera Engine:", cameraBackendCombo);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    form->addRow(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int SettingsDialog::getWidth() const { return widthSpin->value(); }
int SettingsDialog::getHeight() const { return heightSpin->value(); }
double SettingsDialog::getPixelSize() const { return pixelSpin->value(); }
int SettingsDialog::getCameraBackend() const { return cameraBackendCombo->currentIndex(); } // NEW