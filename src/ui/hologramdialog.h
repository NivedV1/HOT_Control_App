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

private slots:
    void generatePattern();
    void saveHologram();

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

    // Data Storage
    QImage currentPhaseImage;
};

#endif // HOLOGRAMDIALOG_H