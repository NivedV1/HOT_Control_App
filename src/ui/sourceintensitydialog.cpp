#include "sourceintensitydialog.h"
#include "components/arrowspinbox.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>

SourceIntensityDialog::SourceIntensityDialog(int slmWidth,
                                             int slmHeight,
                                             const QVector<float> &existingMap,
                                             double initialBeamWaistPx,
                                             QWidget *parent)
    : QDialog(parent),
      slmWidth(slmWidth),
      slmHeight(slmHeight) {
    setWindowTitle("Source Intensity");
    resize(700, 500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QFormLayout *controlsLayout = new QFormLayout();

    presetCombo = new QComboBox();
    presetCombo->addItems({"Gaussian (Standard)", "Gaussian (Tight)", "Gaussian (Wide)"});

    beamWaistSpin = new ArrowDoubleSpinBox();
    beamWaistSpin->setRange(1.0, static_cast<double>(qMax(slmWidth, slmHeight)));
    beamWaistSpin->setDecimals(1);
    beamWaistSpin->setSingleStep(5.0);
    beamWaistSpin->setValue(initialBeamWaistPx > 0.0 ? initialBeamWaistPx : static_cast<double>(qMin(slmWidth, slmHeight)) / 6.0);
    beamWaistSpin->setSuffix(" px");

    controlsLayout->addRow("Preset:", presetCombo);
    controlsLayout->addRow("Beam Waist:", beamWaistSpin);

    previewLabel = new QLabel("Preview");
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumSize(380, 280);
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLabel->setStyleSheet("background-color: black; border: 1px solid #555;");
    previewLabel->setScaledContents(true);

    infoLabel = new QLabel();
    infoLabel->setWordWrap(true);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    applyBtn = new QPushButton("Apply");
    QPushButton *closeBtn = new QPushButton("Close");
    buttonLayout->addWidget(applyBtn);
    buttonLayout->addWidget(closeBtn);

    mainLayout->addLayout(controlsLayout);
    mainLayout->addWidget(previewLabel);
    mainLayout->addWidget(infoLabel);
    mainLayout->addLayout(buttonLayout);

    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SourceIntensityDialog::regeneratePreview);
    connect(beamWaistSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &SourceIntensityDialog::regeneratePreview);
    connect(applyBtn, &QPushButton::clicked, this, &SourceIntensityDialog::applySelection);
    connect(closeBtn, &QPushButton::clicked, this, &SourceIntensityDialog::reject);

    if (existingMap.size() == slmWidth * slmHeight) {
        currentMap = existingMap;
    }

    regeneratePreview();
}

void SourceIntensityDialog::regeneratePreview() {
    const double effectiveWaistPx = beamWaistSpin->value() * presetMultiplier();
    currentMap = buildGaussianMap(effectiveWaistPx);

    currentPreview = QImage(slmWidth, slmHeight, QImage::Format_Grayscale8);
    for (int y = 0; y < slmHeight; ++y) {
        uchar *row = currentPreview.scanLine(y);
        for (int x = 0; x < slmWidth; ++x) {
            const float v = currentMap[y * slmWidth + x];
            const int gray = static_cast<int>(std::round(v * 255.0f));
            row[x] = static_cast<uchar>(qBound(0, gray, 255));
        }
    }

    previewLabel->setPixmap(QPixmap::fromImage(currentPreview));
    infoLabel->setText(QString("Preview size: %1 x %2 (8-bit grayscale). Stored array size: %3")
                           .arg(slmWidth)
                           .arg(slmHeight)
                           .arg(currentMap.size()));
}

void SourceIntensityDialog::applySelection() {
    emit sourceIntensityApplied(currentMap,
                                slmWidth,
                                slmHeight,
                                presetCombo->currentText(),
                                beamWaistSpin->value(),
                                currentPreview);
    accept();
}

QVector<float> SourceIntensityDialog::buildGaussianMap(double effectiveWaistPx) const {
    QVector<float> map;
    map.resize(slmWidth * slmHeight);

    const double cx = (slmWidth - 1) * 0.5;
    const double cy = (slmHeight - 1) * 0.5;
    const double sigma = qMax(1e-6, effectiveWaistPx);
    const double denom = 2.0 * sigma * sigma;

    for (int y = 0; y < slmHeight; ++y) {
        for (int x = 0; x < slmWidth; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const double r2 = dx * dx + dy * dy;
            const double intensity = std::exp(-r2 / denom);
            map[y * slmWidth + x] = static_cast<float>(intensity);
        }
    }

    return map;
}

double SourceIntensityDialog::presetMultiplier() const {
    switch (presetCombo->currentIndex()) {
    case 1:
        return 0.6; // Tight
    case 2:
        return 1.6; // Wide
    default:
        return 1.0; // Standard
    }
}

