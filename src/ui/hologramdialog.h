#ifndef HOLOGRAMDIALOG_H
#define HOLOGRAMDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QImage>
#include <QTimer>
#include <array>

class HologramDialog : public QDialog {
    Q_OBJECT
public:
    explicit HologramDialog(int slmWidth,
                            int slmHeight,
                            bool liveAutoModeEnabled = false,
                            QWidget *parent = nullptr);

// --- NEW: Signal to broadcast the image ---
signals:
    void maskReadyToLoad(const QImage &mask);
    void sendToSLMRequested(const QImage &mask);

private slots:
    void generatePattern();
    void saveHologram();
    void sendToMain(); // NEW: Slot for our new button
    void sendToSLM(); // NEW: Slot for sending directly to SLM
    void updateParameterVisibility(); // Slot to show/hide parameters based on pattern type
    void onAutoGenerateTimeout();
    void addMaskToSlot1();
    void addMaskToSlot2();
    void addMaskToSlot3();
    void clearMaskSlot1();
    void clearMaskSlot2();
    void clearMaskSlot3();
    void clearAllMaskSlots();

private:
    void scheduleAutoGenerate();
    void connectAutoGenerateSignals();
    void updateOutputPreview();
    QImage composeStackMask() const;
    QImage effectiveOutputMask() const;
    void updateStackIndicatorUi();
    void commitGeneratedToSlot(int slotIndex);
    void clearSlot(int slotIndex);

    int targetWidth;
    int targetHeight;
    bool liveAutoMode = false;

    // UI Elements
    QComboBox *patternTypeCombo;
    QDoubleSpinBox *periodSpin;
    QDoubleSpinBox *angleSpin;
    QDoubleSpinBox *focalLengthSpin;
    QDoubleSpinBox *radialPeriodSpin;
    QSpinBox *topologicalChargeSpin;
    QDoubleSpinBox *amplitudeSpin;
    QSpinBox *depthSpin;
    QSpinBox *xOffsetSpin;
    QSpinBox *yOffsetSpin;
    QCheckBox *invertPhaseCheck;
    
    QLabel *phasePreview;
    QPushButton *generateBtn;
    QPushButton *saveBtn;
    QPushButton *sendToMainBtn; // NEW: The button pointer
    QPushButton *sendToSLMBtn; // NEW: Button for sending directly to SLM
    QPushButton *addToSlot1Btn;
    QPushButton *addToSlot2Btn;
    QPushButton *addToSlot3Btn;
    QPushButton *clearSlot1Btn;
    QPushButton *clearSlot2Btn;
    QPushButton *clearSlot3Btn;
    QPushButton *clearAllSlotsBtn;
    QLabel *slot1StatusLabel;
    QLabel *slot2StatusLabel;
    QLabel *slot3StatusLabel;
    QLabel *slot1PreviewLabel;
    QLabel *slot2PreviewLabel;
    QLabel *slot3PreviewLabel;

    // Data Storage
    QImage generatedMask;
    QImage combinedStackMask;
    std::array<QImage, 3> stackSlots;
    std::array<bool, 3> slotFilled = {false, false, false};
    QTimer *autoGenerateTimer = nullptr;
};

#endif // HOLOGRAMDIALOG_H
