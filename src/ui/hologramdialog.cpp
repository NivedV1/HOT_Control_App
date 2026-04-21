#include "hologramdialog.h"
#include "components/arrowspinbox.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <algorithm>
#include <cmath> // Needed for sin, cos, and fmod

// M_PI is sometimes not defined by default in standard C++ math
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

HologramDialog::HologramDialog(int slmWidth, int slmHeight, bool liveAutoModeEnabled, QWidget *parent)
    : QDialog(parent), targetWidth(slmWidth), targetHeight(slmHeight), liveAutoMode(liveAutoModeEnabled) {
    
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
    patternTypeCombo->addItems({"Blazed Grating (Prism)", "Binary Grating", "Fresnel Lens", "Axicon", "Vortex Beam", "Sinusoidal Grating", "Checkerboard", "Four-Quadrant Phase Mask", "Recursive Quadrant Spiral Mask"});
    
    periodSpin = new ArrowDoubleSpinBox();
    periodSpin->setRange(2.0, 1000.0);
    periodSpin->setValue(50.0);
    periodSpin->setSuffix(" pixels");
    
    angleSpin = new ArrowDoubleSpinBox();
    angleSpin->setRange(0.0, 360.0);
    angleSpin->setValue(0.0);
    angleSpin->setSuffix(" °");
    
    // Additional parameters for new patterns
    focalLengthSpin = new ArrowDoubleSpinBox();
    focalLengthSpin->setRange(10.0, 10000.0);
    focalLengthSpin->setValue(500.0);
    focalLengthSpin->setSuffix(" pixels");
    
    radialPeriodSpin = new ArrowDoubleSpinBox();
    radialPeriodSpin->setRange(2.0, 1000.0);
    radialPeriodSpin->setValue(50.0);
    radialPeriodSpin->setSuffix(" pixels");
    
    topologicalChargeSpin = new ArrowSpinBox();
    topologicalChargeSpin->setRange(1, 10);
    topologicalChargeSpin->setValue(1);
    
    amplitudeSpin = new ArrowDoubleSpinBox();
    amplitudeSpin->setRange(0.0, 1.0);
    amplitudeSpin->setValue(0.5);
    amplitudeSpin->setSingleStep(0.1);

    depthSpin = new ArrowSpinBox();
    depthSpin->setRange(1, 32);
    depthSpin->setValue(3);

    xOffsetSpin = new ArrowSpinBox();
    xOffsetSpin->setRange(-targetWidth, targetWidth);
    xOffsetSpin->setValue(0);
    xOffsetSpin->setSuffix(" px");

    yOffsetSpin = new ArrowSpinBox();
    yOffsetSpin->setRange(-targetHeight, targetHeight);
    yOffsetSpin->setValue(0);
    yOffsetSpin->setSuffix(" px");
    
    invertPhaseCheck = new QCheckBox("Invert Phase");
    
    form->addRow("Pattern Type:", patternTypeCombo);
    form->addRow("Grating Period:", periodSpin);
    form->addRow("Rotation Angle:", angleSpin);
    form->addRow("Focal Length:", focalLengthSpin);
    form->addRow("Radial Period:", radialPeriodSpin);
    form->addRow("Topological Charge:", topologicalChargeSpin);
    form->addRow("Amplitude:", amplitudeSpin);
    form->addRow("Depth:", depthSpin);
    form->addRow("X Offset:", xOffsetSpin);
    form->addRow("Y Offset:", yOffsetSpin);
    form->addRow(invertPhaseCheck);
    settingsGroup->setLayout(form);
    
    // Initially hide parameters not relevant to default pattern
    updateParameterVisibility();
    
    // Connect pattern type change to parameter visibility update
    connect(patternTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &HologramDialog::updateParameterVisibility);
    
    QGroupBox *stackGroup = new QGroupBox("Mask Stack");
    QVBoxLayout *stackLayout = new QVBoxLayout();
    stackLayout->setContentsMargins(6, 6, 6, 6);
    stackLayout->setSpacing(4);
    stackGroup->setMaximumHeight(210);

    auto configureSlotPreview = [](QLabel *label) {
        label->setFixedSize(44, 44);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet("background-color: #101010; border: 1px solid #555;");
        label->setText("Empty");
    };

    slot1PreviewLabel = new QLabel();
    slot2PreviewLabel = new QLabel();
    slot3PreviewLabel = new QLabel();
    configureSlotPreview(slot1PreviewLabel);
    configureSlotPreview(slot2PreviewLabel);
    configureSlotPreview(slot3PreviewLabel);

    slot1StatusLabel = new QLabel("S1: Empty");
    slot2StatusLabel = new QLabel("S2: Empty");
    slot3StatusLabel = new QLabel("S3: Empty");

    addToSlot1Btn = new QPushButton("Add S1");
    addToSlot2Btn = new QPushButton("Add S2");
    addToSlot3Btn = new QPushButton("Add S3");
    clearSlot1Btn = new QPushButton("Clr S1");
    clearSlot2Btn = new QPushButton("Clr S2");
    clearSlot3Btn = new QPushButton("Clr S3");
    clearAllSlotsBtn = new QPushButton("Clear All");

    connect(addToSlot1Btn, &QPushButton::clicked, this, &HologramDialog::addMaskToSlot1);
    connect(addToSlot2Btn, &QPushButton::clicked, this, &HologramDialog::addMaskToSlot2);
    connect(addToSlot3Btn, &QPushButton::clicked, this, &HologramDialog::addMaskToSlot3);
    connect(clearSlot1Btn, &QPushButton::clicked, this, &HologramDialog::clearMaskSlot1);
    connect(clearSlot2Btn, &QPushButton::clicked, this, &HologramDialog::clearMaskSlot2);
    connect(clearSlot3Btn, &QPushButton::clicked, this, &HologramDialog::clearMaskSlot3);
    connect(clearAllSlotsBtn, &QPushButton::clicked, this, &HologramDialog::clearAllMaskSlots);

    QGridLayout *slotGrid = new QGridLayout();
    slotGrid->setContentsMargins(0, 0, 0, 0);
    slotGrid->setHorizontalSpacing(4);
    slotGrid->setVerticalSpacing(3);
    slotGrid->addWidget(slot1PreviewLabel, 0, 0);
    slotGrid->addWidget(slot1StatusLabel, 0, 1);
    slotGrid->addWidget(addToSlot1Btn, 0, 2);
    slotGrid->addWidget(clearSlot1Btn, 0, 3);

    slotGrid->addWidget(slot2PreviewLabel, 1, 0);
    slotGrid->addWidget(slot2StatusLabel, 1, 1);
    slotGrid->addWidget(addToSlot2Btn, 1, 2);
    slotGrid->addWidget(clearSlot2Btn, 1, 3);

    slotGrid->addWidget(slot3PreviewLabel, 2, 0);
    slotGrid->addWidget(slot3StatusLabel, 2, 1);
    slotGrid->addWidget(addToSlot3Btn, 2, 2);
    slotGrid->addWidget(clearSlot3Btn, 2, 3);

    stackLayout->addLayout(slotGrid);
    stackLayout->addWidget(clearAllSlotsBtn);
    stackGroup->setLayout(stackLayout);

    generateBtn = new QPushButton("Generate Phase Mask");
    generateBtn->setStyleSheet("QPushButton { font-weight: bold; padding: 10px; background-color: #2b5c8f; color: white; }");
    connect(generateBtn, &QPushButton::clicked, this, &HologramDialog::generatePattern);
    
    leftLayout->addWidget(settingsGroup);
    leftLayout->addWidget(stackGroup);
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

    // --- Output Button Layout ---
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    
    saveBtn = new QPushButton("Save Mask As...");
    saveBtn->setEnabled(false);
    connect(saveBtn, &QPushButton::clicked, this, &HologramDialog::saveHologram);
    
    sendToMainBtn = new QPushButton("Load to Main Screen");
    sendToMainBtn->setEnabled(false);
    connect(sendToMainBtn, &QPushButton::clicked, this, &HologramDialog::sendToMain);
    
    sendToSLMBtn = new QPushButton("Send to SLM");
    sendToSLMBtn->setEnabled(false);
    sendToSLMBtn->setStyleSheet("QPushButton { font-weight: bold; background-color: #dc3545; color: white; }");
    connect(sendToSLMBtn, &QPushButton::clicked, this, &HologramDialog::sendToSLM);
    
    buttonLayout->addWidget(saveBtn);
    buttonLayout->addWidget(sendToMainBtn);
    buttonLayout->addWidget(sendToSLMBtn);
    
    rightLayout->addWidget(rightTitle);
    rightLayout->addWidget(phasePreview);
    rightLayout->addLayout(buttonLayout); // Added the horizontal button layout here

    // Assemble layout
    // Give the right column a ratio of 2 so it takes up more space than the controls
    mainLayout->addLayout(leftLayout, 1);
    mainLayout->addLayout(rightLayout, 2); 

    autoGenerateTimer = new QTimer(this);
    autoGenerateTimer->setSingleShot(true);
    autoGenerateTimer->setInterval(120);
    connect(autoGenerateTimer, &QTimer::timeout, this, &HologramDialog::onAutoGenerateTimeout);

    if (liveAutoMode) {
        connectAutoGenerateSignals();
        scheduleAutoGenerate();
    }

    updateStackIndicatorUi();
    updateOutputPreview();
}

// ==========================================
// MATHEMATICAL GENERATION LOGIC
// ==========================================
void HologramDialog::generatePattern() {
    // Create a blank 8-bit grayscale image matched to your SLM resolution
    generatedMask = QImage(targetWidth, targetHeight, QImage::Format_Grayscale8);
    
    int type = patternTypeCombo->currentIndex();
    double period = periodSpin->value();
    double angleRad = angleSpin->value() * M_PI / 180.0;
    bool invertPhase = invertPhaseCheck->isChecked();
    
    double cosA = std::cos(angleRad);
    double sinA = std::sin(angleRad);
    
    // Pattern centering coordinates
    double centerX = targetWidth / 2.0;
    double centerY = targetHeight / 2.0;
    if (type == 7 || type == 8) {
        centerX += xOffsetSpin->value();
        centerY += yOffsetSpin->value();
    }

    const int spiralDepth = depthSpin->value();

    // Iterate through every pixel of the SLM
    for (int y = 0; y < targetHeight; ++y) {
        uchar *row = generatedMask.scanLine(y);
        for (int x = 0; x < targetWidth; ++x) {
            
            // Calculate rotated projection distance for 1D gratings
            double proj = x * cosA + y * sinA;
            
            // Calculate coordinates relative to center for rotated 2D patterns
            double cx = x - centerX;
            double cy = y - centerY;
            
            // Apply 2D rotation matrix around the center
            double rotX = cx * cosA - cy * sinA;
            double rotY = cx * sinA + cy * cosA;
            
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
                row[x] = (mod < (period/2 )) ? 0 : 128; // 128 = Pi phase shift
            } else if (type == 2) {
                // 3. FRESNEL LENS
                // Centered quadratic phase: phi = (pi / f) * (cx^2 + cy^2)
                const double f = focalLengthSpin->value();
                double phi = (M_PI / f) * (cx * cx + cy * cy);
                if (invertPhase) phi = -phi;
                phi = std::fmod(phi, 2.0 * M_PI);
                if (phi < 0.0) phi += 2.0 * M_PI;
                row[x] = static_cast<uchar>((phi / (2.0 * M_PI)) * 255.0);
            } else if (type == 3) {
                // 4. AXICON
                // Radial-period phase ramp: phi = 2pi * r / radialPeriod
                const double radialPeriod = radialPeriodSpin->value();
                double r = std::sqrt(cx * cx + cy * cy);
                double phi = 2.0 * M_PI * (r / radialPeriod);
                if (invertPhase) phi = -phi;
                phi = std::fmod(phi, 2.0 * M_PI);
                if (phi < 0.0) phi += 2.0 * M_PI;
                row[x] = static_cast<uchar>((phi / (2.0 * M_PI)) * 255.0);
            } else if (type == 4) {
                // 5. VORTEX BEAM
                // Azimuthal phase winding: phi = l * atan2(cy, cx)
                const int l = topologicalChargeSpin->value();
                double phi = l * std::atan2(cy, cx);
                phi = std::fmod(phi, 2.0 * M_PI);
                if (phi < 0.0) phi += 2.0 * M_PI;
                row[x] = static_cast<uchar>((phi / (2.0 * M_PI)) * 255.0);
            } else if (type == 5) {
                // 6. SINUSOIDAL GRATING
                // Phase modulation: phi = pi + A*pi*sin(2*pi*proj/period)
                // Then wrap [0,2pi) and map to [0,255].
                const double amplitude = amplitudeSpin->value();
                const double s = std::sin((2.0 * M_PI * proj) / period);
                double phi = M_PI + (amplitude * M_PI * s);
                phi = std::fmod(phi, 2.0 * M_PI);
                if (phi < 0.0) phi += 2.0 * M_PI;
                row[x] = static_cast<uchar>((phi / (2.0 * M_PI)) * 255.0);
            } else if (type == 6) {
                // 7. CHECKERBOARD
                // Phase alternates between 0 and pi (0 and 128) based on rotated grid
                // Use floor to correctly tile negative space without reflection
                int checkerX = static_cast<int>(std::floor(rotX / period));
                int checkerY = static_cast<int>(std::floor(rotY / period));
                
                // (checkerX + checkerY) % 2 == 0 is slightly problematic with negative numbers 
                // in C++ (% can return negative). Use bitwise check for even/odd parity.
                row[x] = ((std::abs(checkerX + checkerY) % 2) == 0) ? 0 : 128;
            } else if (type == 7) {
                // 8. FOUR-QUADRANT PHASE MASK
                // Using image-style quadrant numbering:
                // Q1 = top-left (0), Q2 = top-right (pi), Q3 = bottom-left (pi), Q4 = bottom-right (0)
                const bool leftHalf = x < centerX;
                const bool topHalf = y < centerY;
                const bool isPiQuadrant = (!leftHalf && topHalf) || (leftHalf && !topHalf);
                row[x] = isPiQuadrant ? 128 : 0;
            } else if (type == 8) {
                // 9. RECURSIVE QUADRANT SPIRAL MASK
                // Apply the same 0/pi, pi/0 quadrant map recursively inside the current bottom-right quadrant.
                double left = 0.0;
                double top = 0.0;
                double right = static_cast<double>(targetWidth);
                double bottom = static_cast<double>(targetHeight);
                double splitX = centerX;
                double splitY = centerY;
                uchar pixelValue = 0;

                for (int level = 0; level < spiralDepth; ++level) {
                    const bool leftHalf = x < splitX;
                    const bool topHalf = y < splitY;
                    const bool isPiQuadrant = (!leftHalf && topHalf) || (leftHalf && !topHalf);
                    pixelValue = isPiQuadrant ? 128 : 0;

                    const bool inFourthQuadrant = !leftHalf && !topHalf;
                    if (!inFourthQuadrant || level == spiralDepth - 1) {
                        break;
                    }

                    left = std::clamp(splitX, left, right);
                    top = std::clamp(splitY, top, bottom);
                    splitX = left + ((right - left) / 2.0);
                    splitY = top + ((bottom - top) / 2.0);
                }

                row[x] = pixelValue;
            }
        }
    }

    updateOutputPreview();

    if (liveAutoMode) {
        // Keep using MainWindow's existing receiveHologram() path so global auto-send
        // logic and SLM safety checks remain centralized in one place.
        const QImage output = effectiveOutputMask();
        if (!output.isNull()) {
            emit maskReadyToLoad(output);
        }
    }
}

void HologramDialog::saveHologram() {
    const QImage output = effectiveOutputMask();
    if (output.isNull()) return;

    QString fileName = QFileDialog::getSaveFileName(this, "Save Phase Mask", "Combined_Mask.bmp", "Images (*.png *.bmp)");
    if (!fileName.isEmpty()) {
        output.save(fileName);
        QMessageBox::information(this, "Success", "Phase mask saved successfully.");
    } 
}

// ==========================================
// NEW: BROADCAST TO MAIN WINDOW
// ==========================================
void HologramDialog::sendToMain() {
    const QImage output = effectiveOutputMask();
    if (!output.isNull()) {
        emit maskReadyToLoad(output); // Send the image data out
        accept(); // Close the dialog
    }
}

// ==========================================
// NEW: SEND DIRECTLY TO SLM
// ==========================================
void HologramDialog::sendToSLM() {
    const QImage output = effectiveOutputMask();
    if (!output.isNull()) {
        emit sendToSLMRequested(output); // Send the image data to SLM
    }
}

// ==========================================
// PARAMETER VISIBILITY MANAGEMENT
// ==========================================
void HologramDialog::updateParameterVisibility() {
    int patternType = patternTypeCombo->currentIndex();
    
    // Hide all parameters by default
    periodSpin->setVisible(false);
    angleSpin->setVisible(false);
    focalLengthSpin->setVisible(false);
    radialPeriodSpin->setVisible(false);
    topologicalChargeSpin->setVisible(false);
    amplitudeSpin->setVisible(false);
    depthSpin->setVisible(false);
    xOffsetSpin->setVisible(false);
    yOffsetSpin->setVisible(false);
    invertPhaseCheck->setVisible(false);
    
    // Show relevant parameters based on pattern type
    switch (patternType) {
        case 0: // Blazed Grating
        case 1: // Binary Grating
            periodSpin->setVisible(true);
            angleSpin->setVisible(true);
            break;
        case 2: // Fresnel Lens
            focalLengthSpin->setVisible(true);
            invertPhaseCheck->setVisible(true);
            break;
        case 3: // Axicon
            radialPeriodSpin->setVisible(true);
            invertPhaseCheck->setVisible(true);
            break;
        case 4: // Vortex Beam
            topologicalChargeSpin->setVisible(true);
            break;
        case 5: // Sinusoidal Grating
            periodSpin->setVisible(true);
            angleSpin->setVisible(true);
            amplitudeSpin->setVisible(true);
            break;
        case 6: // Checkerboard
            periodSpin->setVisible(true);
            angleSpin->setVisible(true);
            break;
        case 7: // Four-Quadrant Phase Mask
            xOffsetSpin->setVisible(true);
            yOffsetSpin->setVisible(true);
            break;
        case 8: // Recursive Quadrant Spiral Mask
            depthSpin->setVisible(true);
            xOffsetSpin->setVisible(true);
            yOffsetSpin->setVisible(true);
            break;
    }
}

void HologramDialog::connectAutoGenerateSignals() {
    connect(patternTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(periodSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(angleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(focalLengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(radialPeriodSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(topologicalChargeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(amplitudeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(depthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(xOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(yOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &HologramDialog::scheduleAutoGenerate);
    connect(invertPhaseCheck, &QCheckBox::toggled, this, &HologramDialog::scheduleAutoGenerate);
}

void HologramDialog::scheduleAutoGenerate() {
    if (!liveAutoMode || !autoGenerateTimer) {
        return;
    }
    autoGenerateTimer->start();
}

void HologramDialog::onAutoGenerateTimeout() {
    if (!liveAutoMode) {
        return;
    }
    generatePattern();
}

void HologramDialog::addMaskToSlot1() {
    commitGeneratedToSlot(0);
}

void HologramDialog::addMaskToSlot2() {
    commitGeneratedToSlot(1);
}

void HologramDialog::addMaskToSlot3() {
    commitGeneratedToSlot(2);
}

void HologramDialog::clearMaskSlot1() {
    clearSlot(0);
}

void HologramDialog::clearMaskSlot2() {
    clearSlot(1);
}

void HologramDialog::clearMaskSlot3() {
    clearSlot(2);
}

void HologramDialog::clearAllMaskSlots() {
    for (int i = 0; i < 3; ++i) {
        stackSlots[i] = QImage();
        slotFilled[i] = false;
    }
    combinedStackMask = composeStackMask();
    updateStackIndicatorUi();
    updateOutputPreview();

    if (liveAutoMode) {
        const QImage output = effectiveOutputMask();
        if (!output.isNull()) {
            emit maskReadyToLoad(output);
        }
    }
}

QImage HologramDialog::composeStackMask() const {
    bool hasAnySlot = false;
    for (int i = 0; i < 3; ++i) {
        if (slotFilled[i] && !stackSlots[i].isNull()) {
            hasAnySlot = true;
            break;
        }
    }
    if (!hasAnySlot) {
        return QImage();
    }

    QImage result(targetWidth, targetHeight, QImage::Format_Grayscale8);
    result.fill(0);

    for (int i = 0; i < 3; ++i) {
        if (!slotFilled[i] || stackSlots[i].isNull()) {
            continue;
        }

        QImage slotImage = stackSlots[i].convertToFormat(QImage::Format_Grayscale8);
        if (slotImage.size() != result.size()) {
            slotImage = slotImage.scaled(result.size());
        }

        for (int y = 0; y < result.height(); ++y) {
            uchar *dstRow = result.scanLine(y);
            const uchar *srcRow = slotImage.constScanLine(y);
            for (int x = 0; x < result.width(); ++x) {
                dstRow[x] = static_cast<uchar>(dstRow[x] + srcRow[x]);
            }
        }
    }

    return result;
}

QImage HologramDialog::effectiveOutputMask() const {
    if (!combinedStackMask.isNull()) {
        return combinedStackMask;
    }
    return generatedMask;
}

void HologramDialog::updateStackIndicatorUi() {
    QLabel *statusLabels[3] = {slot1StatusLabel, slot2StatusLabel, slot3StatusLabel};
    QLabel *previewLabels[3] = {slot1PreviewLabel, slot2PreviewLabel, slot3PreviewLabel};
    QPushButton *clearButtons[3] = {clearSlot1Btn, clearSlot2Btn, clearSlot3Btn};

    bool anyFilled = false;
    for (int i = 0; i < 3; ++i) {
        if (slotFilled[i] && !stackSlots[i].isNull()) {
            anyFilled = true;
            statusLabels[i]->setText(QString("S%1: Filled").arg(i + 1));
            previewLabels[i]->setText("");
            previewLabels[i]->setPixmap(QPixmap::fromImage(stackSlots[i]).scaled(
                previewLabels[i]->size(),
                Qt::IgnoreAspectRatio,
                Qt::SmoothTransformation));
            clearButtons[i]->setEnabled(true);
        } else {
            slotFilled[i] = false;
            statusLabels[i]->setText(QString("S%1: Empty").arg(i + 1));
            previewLabels[i]->setPixmap(QPixmap());
            previewLabels[i]->setText("Empty");
            clearButtons[i]->setEnabled(false);
        }
    }

    clearAllSlotsBtn->setEnabled(anyFilled);
}

void HologramDialog::commitGeneratedToSlot(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= 3) {
        return;
    }
    if (generatedMask.isNull()) {
        QMessageBox::warning(this, "No Mask", "Generate a phase mask before adding to a slot.");
        return;
    }

    stackSlots[slotIndex] = generatedMask.copy();
    slotFilled[slotIndex] = true;
    combinedStackMask = composeStackMask();
    updateStackIndicatorUi();
    updateOutputPreview();

    if (liveAutoMode) {
        const QImage output = effectiveOutputMask();
        if (!output.isNull()) {
            emit maskReadyToLoad(output);
        }
    }
}

void HologramDialog::clearSlot(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= 3) {
        return;
    }

    stackSlots[slotIndex] = QImage();
    slotFilled[slotIndex] = false;
    combinedStackMask = composeStackMask();
    updateStackIndicatorUi();
    updateOutputPreview();

    if (liveAutoMode) {
        const QImage output = effectiveOutputMask();
        if (!output.isNull()) {
            emit maskReadyToLoad(output);
        }
    }
}

void HologramDialog::updateOutputPreview() {
    combinedStackMask = composeStackMask();
    const QImage output = effectiveOutputMask();

    if (output.isNull()) {
        phasePreview->setPixmap(QPixmap());
        phasePreview->setText("Click Generate");
        saveBtn->setEnabled(false);
        sendToMainBtn->setEnabled(false);
        sendToSLMBtn->setEnabled(false);
        return;
    }

    phasePreview->setText("");
    phasePreview->setPixmap(QPixmap::fromImage(output));
    saveBtn->setEnabled(true);
    sendToMainBtn->setEnabled(true);
    sendToSLMBtn->setEnabled(true);
}

