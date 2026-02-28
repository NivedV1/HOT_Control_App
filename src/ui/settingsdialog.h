#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(int currentWidth, int currentHeight, double currentPixelSize, QWidget *parent = nullptr);

    // Getters for the main window to fetch the new values
    int getWidth() const;
    int getHeight() const;
    double getPixelSize() const;

private:
    QSpinBox *widthSpin;
    QSpinBox *heightSpin;
    QDoubleSpinBox *pixelSpin;
};

#endif // SETTINGSDIALOG_H