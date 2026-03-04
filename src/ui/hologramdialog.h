#ifndef HOLOGRAMDIALOG_H
#define HOLOGRAMDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QImage>

class HologramDialog : public QDialog {
    Q_OBJECT
public:
    explicit HologramDialog(int slmWidth, int slmHeight, QWidget *parent = nullptr);

// --- NEW: Signal to broadcast the image ---
signals:
    void maskReadyToLoad(const QImage &mask);

private slots:
    void generatePattern();
    void saveHologram();
    void sendToMain(); // NEW: Slot for our new button

private:
    int targetWidth;
    int targetHeight;

    // UI Elements
    QComboBox *patternTypeCombo;
    QDoubleSpinBox *periodSpin;
    QDoubleSpinBox *angleSpin;
    
    QLabel *phasePreview;
    QPushButton *generateBtn;
    QPushButton *saveBtn;
    QPushButton *sendToMainBtn; // NEW: The button pointer

    // Data Storage
    QImage currentPhaseImage;
};

#endif // HOLOGRAMDIALOG_H