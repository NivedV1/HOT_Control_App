#include "hologramdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <cmath> // Needed for sin, cos, and fmod

// M_PI is sometimes not defined by default in standard C++ math
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

HologramDialog::HologramDialog(int slmWidth, int slmHeight, QWidget *parent)
    : QDialog(parent), targetWidth(slmWidth), targetHeight(slmHeight) {
    
    setWindowTitle("Standard Phase Pattern Generator");
    resize(800, 500); 

    QHBoxLayout *mainLayout = new QHBoxLayout(this);

    // ==========================================
    // LEFT COLUMN: Mathematical Controls
    // ==========================================
    QVBoxLayout *leftLayout = new QVBoxLayout();
    
    QGroupBox *settingsGroup = new QGroupBox("Pattern Parameters");
    QFormLayout *form = new QFormLayout();
    
    patternTypeCombo = new QComboBox();
    patternTypeCombo->addItems({"Blazed Grating (Prism)", "Binary Grating"});
    
    periodSpin = new QDoubleSpinBox();
    periodSpin->setRange(2.0, 1000.0);
    periodSpin->setValue(50.0);
    periodSpin->setSuffix(" pixels");
    
    angleSpin = new QDoubleSpinBox();
    angleSpin->setRange(0.0, 360.0);
    angleSpin->setValue(0.0);
    angleSpin->setSuffix(" °");
    
    form->addRow("Pattern Type:", patternTypeCombo);
    form->addRow("Grating Period:", periodSpin);
    form->addRow("Rotation Angle:", angleSpin);
    settingsGroup->setLayout(form);
    
    generateBtn = new QPushButton("Generate Phase Mask");
    generateBtn->setStyleSheet("QPushButton { font-weight: bold; padding: 10px; background-color: #2b5c8f; color: white; }");
    connect(generateBtn, &QPushButton::clicked, this, &HologramDialog::generatePattern);
    
    leftLayout->addWidget(settingsGroup);
    leftLayout->addWidget(generateBtn);
    leftLayout->addStretch(); // Keeps controls packed neatly at the top

    // ==========================================
    // RIGHT COLUMN: Output Preview
    // ==========================================
    QVBoxLayout *rightLayout = new QVBoxLayout();
    QLabel *rightTitle = new QLabel(QString("Generated Mask (%1 x %2)").arg(slmWidth).arg(slmHeight));
    rightTitle->setAlignment(Qt::AlignCenter);
    
    phasePreview = new QLabel("Click Generate");
    phasePreview->setAlignment(Qt::AlignCenter);
    
    // Force the black box to expand and push the title/button to the edges
    phasePreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); 
    phasePreview->setStyleSheet("background-color: black; border: 1px solid #555;");
    phasePreview->setMinimumSize(350, 350);
    phasePreview->setScaledContents(true);
    
    // --- NEW: Button Layout ---
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    saveBtn = new QPushButton("Save Mask As...");
    saveBtn->setEnabled(false);
    connect(saveBtn, &QPushButton::clicked, this, &HologramDialog::saveHologram);
    
    sendToMainBtn = new QPushButton("Load to Main Screen");
    sendToMainBtn->setEnabled(false);
    connect(sendToMainBtn, &QPushButton::clicked, this, &HologramDialog::sendToMain);
    
    buttonLayout->addWidget(saveBtn);
    buttonLayout->addWidget(sendToMainBtn);
    
    rightLayout->addWidget(rightTitle);
    rightLayout->addWidget(phasePreview);
    rightLayout->addLayout(buttonLayout); // Added the horizontal button layout here

    // Assemble layout
    // Give the right column a ratio of 2 so it takes up more space than the controls
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 2); 
}

// ==========================================
// MATHEMATICAL GENERATION LOGIC
// ==========================================
void HologramDialog::generatePattern() {
    // Create a blank 8-bit grayscale image matched to your SLM resolution
    currentPhaseImage = QImage(targetWidth, targetHeight, QImage::Format_Grayscale8);
    
    int type = patternTypeCombo->currentIndex();
    double period = periodSpin->value();
    double angleRad = angleSpin->value() * M_PI / 180.0;
    
    double cosA = std::cos(angleRad);
    double sinA = std::sin(angleRad);

    // Iterate through every pixel of the SLM
    for (int y = 0; y < targetHeight; ++y) {
        uchar *row = currentPhaseImage.scanLine(y);
        for (int x = 0; x < targetWidth; ++x) {
            
            // Calculate rotated projection distance for 1D gratings
            double proj = x * cosA + y * sinA;
            
            if (type == 0) {
                // 1. BLAZED GRATING
                // Phase increases linearly from 0 to 2pi (0 to 255) over the period
                double phase = std::fmod(proj, period) / period;
                if (phase < 0) phase += 1.0; // Correct negative modulo wraps
                row[x] = static_cast<uchar>(phase * 255.0);
                
            } else if (type == 1) {
                // 2. BINARY GRATING
                // Phase jumps between 0 and pi (0 and 128) halfway through the period
                double mod = std::fmod(proj, period);
                if (mod < 0) mod += period;
                row[x] = (mod < (period / 2.0)) ? 0 : 128; // 128 = Pi phase shift
            }
        }
    }

    // Update the UI
    phasePreview->setPixmap(QPixmap::fromImage(currentPhaseImage));
    saveBtn->setEnabled(true);
    sendToMainBtn->setEnabled(true); // --- NEW: Enable the Load button ---
}

void HologramDialog::saveHologram() {
    if (currentPhaseImage.isNull()) return;

    QString fileName = QFileDialog::getSaveFileName(this, "Save Phase Mask", "Grating_Mask.bmp", "Images (*.png *.bmp)");
    if (!fileName.isEmpty()) {
        currentPhaseImage.save(fileName);
        QMessageBox::information(this, "Success", "Phase mask saved successfully.");
    } 
}

// ==========================================
// NEW: BROADCAST TO MAIN WINDOW
// ==========================================
void HologramDialog::sendToMain() {
    if (!currentPhaseImage.isNull()) {
        emit maskReadyToLoad(currentPhaseImage); // Send the image data out
        accept(); // Close the dialog
    }
}