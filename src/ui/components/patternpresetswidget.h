#ifndef PATTERNPRESETSWIDGET_H
#define PATTERNPRESETSWIDGET_H

#include <QPointF>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QStackedWidget;

class PatternPresetsWidget : public QWidget {
    Q_OBJECT

public:
    explicit PatternPresetsWidget(int cameraWidth, int cameraHeight, QWidget *parent = nullptr);

    void setCameraResolution(int cameraWidth, int cameraHeight);

signals:
    void patternGenerated(const QVector<QPointF> &points, const QString &summary);

private slots:
    void onPresetChanged(int index);
    void onGenerateClicked();
    void onTriangleSymmetricChanged(bool checked);

private:
    enum PresetIndex {
        CircleIndex = 0,
        TriangleIndex,
        SquareIndex,
        RectangleIndex,
        HexagonIndex,
        TwoSpotsIndex,
        StarIndex,
        PlanetAndMoonIndex
    };

    QWidget *createCirclePage();
    QWidget *createTrianglePage();
    QWidget *createSquarePage();
    QWidget *createRectanglePage();
    QWidget *createHexagonPage();
    QWidget *createTwoSpotsPage();
    QWidget *createStarPage();
    QWidget *createPlanetMoonPage();

    void updateLimits();
    QString presetName(int index) const;

    int cameraWidth;
    int cameraHeight;

    QComboBox *presetCombo;
    QStackedWidget *optionsStack;
    QPushButton *generateBtn;

    QDoubleSpinBox *circleRadiusSpin;
    QSpinBox *circlePointsSpin;
    QDoubleSpinBox *circleRotationSpin;
    QDoubleSpinBox *circleXShiftSpin;
    QDoubleSpinBox *circleYShiftSpin;

    QCheckBox *triangleSymmetricCheck;
    QDoubleSpinBox *triangleScaleSpin;
    QSpinBox *trianglePointsSpin;
    QDoubleSpinBox *triangleRotationSpin;
    QDoubleSpinBox *triangleXShiftSpin;
    QDoubleSpinBox *triangleYShiftSpin;

    QDoubleSpinBox *squareSizeSpin;
    QSpinBox *squarePointsSpin;
    QDoubleSpinBox *squareRotationSpin;
    QDoubleSpinBox *squareXShiftSpin;
    QDoubleSpinBox *squareYShiftSpin;

    QDoubleSpinBox *rectWidthSpin;
    QDoubleSpinBox *rectHeightSpin;
    QSpinBox *rectPointsSpin;
    QDoubleSpinBox *rectRotationSpin;
    QDoubleSpinBox *rectXShiftSpin;
    QDoubleSpinBox *rectYShiftSpin;

    QDoubleSpinBox *hexRadiusSpin;
    QSpinBox *hexPointsSpin;
    QDoubleSpinBox *hexRotationSpin;
    QDoubleSpinBox *hexXShiftSpin;
    QDoubleSpinBox *hexYShiftSpin;

    QDoubleSpinBox *twoSpotsDistanceSpin;
    QDoubleSpinBox *twoSpotsRotationSpin;
    QDoubleSpinBox *twoSpotsXShiftSpin;
    QDoubleSpinBox *twoSpotsYShiftSpin;

    QSpinBox *starPointsSpin;
    QDoubleSpinBox *starOuterRadiusSpin;
    QDoubleSpinBox *starInnerRadiusSpin;
    QSpinBox *starTotalPointsSpin;
    QDoubleSpinBox *starRotationSpin;
    QDoubleSpinBox *starXShiftSpin;
    QDoubleSpinBox *starYShiftSpin;

    QDoubleSpinBox *pmPlanetRadiusSpin;
    QDoubleSpinBox *pmMoonRadiusSpin;
    QDoubleSpinBox *pmDistanceSpin;
    QSpinBox *pmTotalPointsSpin;
    QDoubleSpinBox *pmRotationSpin;
    QDoubleSpinBox *pmXShiftSpin;
    QDoubleSpinBox *pmYShiftSpin;
};

#endif // PATTERNPRESETSWIDGET_H