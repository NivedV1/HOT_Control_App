#include "settingsdialog.h"
#include <QFormLayout>
#include <QDialogButtonBox>

SettingsDialog::SettingsDialog(int currentWidth, int currentHeight, double currentPixelSize, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle("Hardware Settings");
    setMinimumWidth(300);
    
    QFormLayout *form = new QFormLayout(this);
    
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
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    form->addRow(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int SettingsDialog::getWidth() const { return widthSpin->value(); }
int SettingsDialog::getHeight() const { return heightSpin->value(); }
double SettingsDialog::getPixelSize() const { return pixelSpin->value(); }