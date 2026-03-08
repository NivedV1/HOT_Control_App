#ifndef SOURCEINTENSITYDIALOG_H
#define SOURCEINTENSITYDIALOG_H

#include <QDialog>
#include <QVector>
#include <QImage>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

class SourceIntensityDialog : public QDialog {
    Q_OBJECT

public:
    explicit SourceIntensityDialog(int slmWidth,
                                   int slmHeight,
                                   const QVector<float> &existingMap,
                                   double initialBeamWaistPx,
                                   QWidget *parent = nullptr);

signals:
    void sourceIntensityApplied(const QVector<float> &intensityMap,
                                int width,
                                int height,
                                const QString &presetName,
                                double beamWaistPx,
                                const QImage &previewImage);

private slots:
    void regeneratePreview();
    void applySelection();

private:
    QVector<float> buildGaussianMap(double effectiveWaistPx) const;
    double presetMultiplier() const;

    int slmWidth;
    int slmHeight;

    QComboBox *presetCombo;
    QDoubleSpinBox *beamWaistSpin;
    QLabel *previewLabel;
    QLabel *infoLabel;
    QPushButton *applyBtn;

    QVector<float> currentMap;
    QImage currentPreview;
};

#endif // SOURCEINTENSITYDIALOG_H
