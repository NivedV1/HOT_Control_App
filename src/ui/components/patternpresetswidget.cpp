#include "patternpresetswidget.h"

#include "patterngenerator.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtGlobal>

PatternPresetsWidget::PatternPresetsWidget(int cameraWidth, int cameraHeight, QWidget *parent)
    : QWidget(parent),
      cameraWidth(cameraWidth),
      cameraHeight(cameraHeight),
      presetCombo(nullptr),
      optionsStack(nullptr),
      generateBtn(nullptr),
      circleRadiusSpin(nullptr),
      circlePointsSpin(nullptr),
      circleRotationSpin(nullptr),
      circleXShiftSpin(nullptr),
      circleYShiftSpin(nullptr),
      triangleSymmetricCheck(nullptr),
      triangleScaleSpin(nullptr),
      trianglePointsSpin(nullptr),
      triangleRotationSpin(nullptr),
      triangleXShiftSpin(nullptr),
      triangleYShiftSpin(nullptr),
      squareSizeSpin(nullptr),
      squarePointsSpin(nullptr),
      squareRotationSpin(nullptr),
      squareXShiftSpin(nullptr),
      squareYShiftSpin(nullptr),
      rectWidthSpin(nullptr),
      rectHeightSpin(nullptr),
      rectPointsSpin(nullptr),
      rectRotationSpin(nullptr),
      rectXShiftSpin(nullptr),
      rectYShiftSpin(nullptr),
      hexRadiusSpin(nullptr),
      hexPointsSpin(nullptr),
      hexRotationSpin(nullptr),
      hexXShiftSpin(nullptr),
      hexYShiftSpin(nullptr),
      twoSpotsDistanceSpin(nullptr),
      twoSpotsRotationSpin(nullptr),
      twoSpotsXShiftSpin(nullptr),
      twoSpotsYShiftSpin(nullptr),
      starPointsSpin(nullptr),
      starOuterRadiusSpin(nullptr),
      starInnerRadiusSpin(nullptr),
      starTotalPointsSpin(nullptr),
      starRotationSpin(nullptr),
      starXShiftSpin(nullptr),
      starYShiftSpin(nullptr),
      pmPlanetRadiusSpin(nullptr),
      pmMoonRadiusSpin(nullptr),
      pmDistanceSpin(nullptr),
      pmTotalPointsSpin(nullptr),
      pmRotationSpin(nullptr),
      pmXShiftSpin(nullptr),
      pmYShiftSpin(nullptr),
      gridRowsSpin(nullptr),
      gridColsSpin(nullptr),
      gridRowSpacingSpin(nullptr),
      gridColSpacingSpin(nullptr),
      gridCenterCheck(nullptr),
      gridRotationSpin(nullptr),
      gridXShiftSpin(nullptr),
      gridYShiftSpin(nullptr) {

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *helpText = new QLabel("Pick a preset, tune parameters, and click Generate to replace current grid points.");
    helpText->setWordWrap(true);
    helpText->setStyleSheet("font-size: 11px;");

    QHBoxLayout *presetRow = new QHBoxLayout();
    QLabel *presetLabel = new QLabel("Preset:");
    presetCombo = new QComboBox();
    presetCombo->addItems({"Circle", "Triangle", "Square", "Rectangle", "Hexagon", "Two Spots", "Star", "Planet & Moon", "N*M Grid"});
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(presetCombo, 1);

    optionsStack = new QStackedWidget();
    optionsStack->addWidget(createCirclePage());
    optionsStack->addWidget(createTrianglePage());
    optionsStack->addWidget(createSquarePage());
    optionsStack->addWidget(createRectanglePage());
    optionsStack->addWidget(createHexagonPage());
    optionsStack->addWidget(createTwoSpotsPage());
    optionsStack->addWidget(createStarPage());
    optionsStack->addWidget(createPlanetMoonPage());
    optionsStack->addWidget(createGridPage());

    generateBtn = new QPushButton("Generate");

    mainLayout->addWidget(helpText);
    mainLayout->addLayout(presetRow);
    mainLayout->addWidget(optionsStack);
    mainLayout->addWidget(generateBtn);
    mainLayout->addStretch();

    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PatternPresetsWidget::onPresetChanged);
    connect(generateBtn, &QPushButton::clicked, this, &PatternPresetsWidget::onGenerateClicked);
    connect(triangleSymmetricCheck, &QCheckBox::toggled,
            this, &PatternPresetsWidget::onTriangleSymmetricChanged);

    updateLimits();
    onPresetChanged(presetCombo->currentIndex());
    onTriangleSymmetricChanged(triangleSymmetricCheck->isChecked());
}

void PatternPresetsWidget::setCameraResolution(int cameraWidth, int cameraHeight) {
    this->cameraWidth = cameraWidth;
    this->cameraHeight = cameraHeight;
    updateLimits();
}

void PatternPresetsWidget::onPresetChanged(int index) {
    optionsStack->setCurrentIndex(index);
}

void PatternPresetsWidget::onGenerateClicked() {
    PatternGenerator::PatternRequest request;

    const int index = presetCombo->currentIndex();
    switch (index) {
    case CircleIndex:
        request.preset = PatternGenerator::Preset::Circle;
        request.radius = circleRadiusSpin->value();
        request.pointCount = circlePointsSpin->value();
        request.rotationDeg = circleRotationSpin->value();
        request.xShift = circleXShiftSpin->value();
        request.yShift = circleYShiftSpin->value();
        break;

    case TriangleIndex:
        request.preset = PatternGenerator::Preset::Triangle;
        request.symmetric = triangleSymmetricCheck->isChecked();
        request.scale = triangleScaleSpin->value();
        request.pointCount = trianglePointsSpin->value();
        request.rotationDeg = triangleRotationSpin->value();
        request.xShift = triangleXShiftSpin->value();
        request.yShift = triangleYShiftSpin->value();
        break;

    case SquareIndex:
        request.preset = PatternGenerator::Preset::Square;
        request.size = squareSizeSpin->value();
        request.pointCount = squarePointsSpin->value();
        request.rotationDeg = squareRotationSpin->value();
        request.xShift = squareXShiftSpin->value();
        request.yShift = squareYShiftSpin->value();
        break;

    case RectangleIndex:
        request.preset = PatternGenerator::Preset::Rectangle;
        request.width = rectWidthSpin->value();
        request.height = rectHeightSpin->value();
        request.pointCount = rectPointsSpin->value();
        request.rotationDeg = rectRotationSpin->value();
        request.xShift = rectXShiftSpin->value();
        request.yShift = rectYShiftSpin->value();
        break;

    case HexagonIndex:
        request.preset = PatternGenerator::Preset::Hexagon;
        request.radius = hexRadiusSpin->value();
        request.pointCount = hexPointsSpin->value();
        request.rotationDeg = hexRotationSpin->value();
        request.xShift = hexXShiftSpin->value();
        request.yShift = hexYShiftSpin->value();
        break;

    case TwoSpotsIndex:
        request.preset = PatternGenerator::Preset::TwoSpots;
        request.distance = twoSpotsDistanceSpin->value();
        request.pointCount = 2;
        request.rotationDeg = twoSpotsRotationSpin->value();
        request.xShift = twoSpotsXShiftSpin->value();
        request.yShift = twoSpotsYShiftSpin->value();
        break;

    case StarIndex:
        request.preset = PatternGenerator::Preset::Star;
        request.starPoints = starPointsSpin->value();
        request.radius = starOuterRadiusSpin->value();
        request.innerRadius = starInnerRadiusSpin->value();
        request.pointCount = starTotalPointsSpin->value();
        request.rotationDeg = starRotationSpin->value();
        request.xShift = starXShiftSpin->value();
        request.yShift = starYShiftSpin->value();
        break;

    case PlanetAndMoonIndex:
        request.preset = PatternGenerator::Preset::PlanetAndMoon;
        request.radius = pmPlanetRadiusSpin->value();
        request.moonRadius = pmMoonRadiusSpin->value();
        request.distance = pmDistanceSpin->value();
        request.pointCount = pmTotalPointsSpin->value();
        request.rotationDeg = pmRotationSpin->value();
        request.xShift = pmXShiftSpin->value();
        request.yShift = pmYShiftSpin->value();
        break;

    case GridIndex:
    default:
        request.preset = PatternGenerator::Preset::Grid;
        request.gridRows = gridRowsSpin->value();
        request.gridCols = gridColsSpin->value();
        request.gridRowSpacing = gridRowSpacingSpin->value();
        request.gridColSpacing = gridColSpacingSpin->value();
        request.centerGridAtOrigin = gridCenterCheck->isChecked();
        request.rotationDeg = gridRotationSpin->value();
        request.xShift = gridXShiftSpin->value();
        request.yShift = gridYShiftSpin->value();
        // Point count is determined automatically by Rows * Cols, but the summary uses request.pointCount if you want to set it, or points.size()
        request.pointCount = request.gridRows * request.gridCols;
        break;
    }

    const QVector<QPointF> points = PatternGenerator::generate(request);
    const QString summary = QString("Generated %1 with %2 point(s).")
                                .arg(presetName(index))
                                .arg(points.size());

    emit patternGenerated(points, summary);
}

void PatternPresetsWidget::onTriangleSymmetricChanged(bool checked) {
    triangleXShiftSpin->setEnabled(!checked);
    triangleYShiftSpin->setEnabled(!checked);

    if (checked) {
        triangleXShiftSpin->setValue(0.0);
        triangleYShiftSpin->setValue(0.0);
    }
}

QWidget *PatternPresetsWidget::createCirclePage() {
    QGroupBox *group = new QGroupBox("Circle");
    QFormLayout *form = new QFormLayout(group);

    circleRadiusSpin = new QDoubleSpinBox();
    circleRadiusSpin->setDecimals(1);
    circleRadiusSpin->setSingleStep(1.0);
    circleRadiusSpin->setValue(120.0);

    circlePointsSpin = new QSpinBox();
    circlePointsSpin->setRange(3, 2000);
    circlePointsSpin->setValue(24);

    circleRotationSpin = new QDoubleSpinBox();
    circleRotationSpin->setRange(-360.0, 360.0);
    circleRotationSpin->setDecimals(1);
    circleRotationSpin->setSingleStep(1.0);

    circleXShiftSpin = new QDoubleSpinBox();
    circleXShiftSpin->setDecimals(1);
    circleXShiftSpin->setSingleStep(1.0);

    circleYShiftSpin = new QDoubleSpinBox();
    circleYShiftSpin->setDecimals(1);
    circleYShiftSpin->setSingleStep(1.0);

    form->addRow("Radius:", circleRadiusSpin);
    form->addRow("No. of points:", circlePointsSpin);
    form->addRow("Rotation (deg):", circleRotationSpin);
    form->addRow("X shift:", circleXShiftSpin);
    form->addRow("Y shift:", circleYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createTrianglePage() {
    QGroupBox *group = new QGroupBox("Triangle");
    QFormLayout *form = new QFormLayout(group);

    triangleSymmetricCheck = new QCheckBox("Symmetric around center (0,0)");
    triangleSymmetricCheck->setChecked(true);

    triangleScaleSpin = new QDoubleSpinBox();
    triangleScaleSpin->setDecimals(1);
    triangleScaleSpin->setSingleStep(1.0);
    triangleScaleSpin->setValue(120.0);

    trianglePointsSpin = new QSpinBox();
    trianglePointsSpin->setRange(3, 2000);
    trianglePointsSpin->setValue(30);

    triangleRotationSpin = new QDoubleSpinBox();
    triangleRotationSpin->setRange(-360.0, 360.0);
    triangleRotationSpin->setDecimals(1);
    triangleRotationSpin->setSingleStep(1.0);

    triangleXShiftSpin = new QDoubleSpinBox();
    triangleXShiftSpin->setDecimals(1);
    triangleXShiftSpin->setSingleStep(1.0);

    triangleYShiftSpin = new QDoubleSpinBox();
    triangleYShiftSpin->setDecimals(1);
    triangleYShiftSpin->setSingleStep(1.0);

    form->addRow("Symmetric:", triangleSymmetricCheck);
    form->addRow("Scale:", triangleScaleSpin);
    form->addRow("No. of points:", trianglePointsSpin);
    form->addRow("Rotation (deg):", triangleRotationSpin);
    form->addRow("X shift:", triangleXShiftSpin);
    form->addRow("Y shift:", triangleYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createSquarePage() {
    QGroupBox *group = new QGroupBox("Square");
    QFormLayout *form = new QFormLayout(group);

    squareSizeSpin = new QDoubleSpinBox();
    squareSizeSpin->setDecimals(1);
    squareSizeSpin->setSingleStep(1.0);
    squareSizeSpin->setValue(160.0);

    squarePointsSpin = new QSpinBox();
    squarePointsSpin->setRange(4, 2000);
    squarePointsSpin->setValue(40);

    squareRotationSpin = new QDoubleSpinBox();
    squareRotationSpin->setRange(-360.0, 360.0);
    squareRotationSpin->setDecimals(1);
    squareRotationSpin->setSingleStep(1.0);

    squareXShiftSpin = new QDoubleSpinBox();
    squareXShiftSpin->setDecimals(1);
    squareXShiftSpin->setSingleStep(1.0);

    squareYShiftSpin = new QDoubleSpinBox();
    squareYShiftSpin->setDecimals(1);
    squareYShiftSpin->setSingleStep(1.0);

    form->addRow("Size:", squareSizeSpin);
    form->addRow("No. of points:", squarePointsSpin);
    form->addRow("Rotation (deg):", squareRotationSpin);
    form->addRow("X shift:", squareXShiftSpin);
    form->addRow("Y shift:", squareYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createRectanglePage() {
    QGroupBox *group = new QGroupBox("Rectangle");
    QFormLayout *form = new QFormLayout(group);

    rectWidthSpin = new QDoubleSpinBox();
    rectWidthSpin->setDecimals(1);
    rectWidthSpin->setSingleStep(1.0);
    rectWidthSpin->setValue(240.0);

    rectHeightSpin = new QDoubleSpinBox();
    rectHeightSpin->setDecimals(1);
    rectHeightSpin->setSingleStep(1.0);
    rectHeightSpin->setValue(140.0);

    rectPointsSpin = new QSpinBox();
    rectPointsSpin->setRange(4, 2000);
    rectPointsSpin->setValue(48);

    rectRotationSpin = new QDoubleSpinBox();
    rectRotationSpin->setRange(-360.0, 360.0);
    rectRotationSpin->setDecimals(1);
    rectRotationSpin->setSingleStep(1.0);

    rectXShiftSpin = new QDoubleSpinBox();
    rectXShiftSpin->setDecimals(1);
    rectXShiftSpin->setSingleStep(1.0);

    rectYShiftSpin = new QDoubleSpinBox();
    rectYShiftSpin->setDecimals(1);
    rectYShiftSpin->setSingleStep(1.0);

    form->addRow("Width:", rectWidthSpin);
    form->addRow("Height:", rectHeightSpin);
    form->addRow("No. of points:", rectPointsSpin);
    form->addRow("Rotation (deg):", rectRotationSpin);
    form->addRow("X shift:", rectXShiftSpin);
    form->addRow("Y shift:", rectYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createHexagonPage() {
    QGroupBox *group = new QGroupBox("Hexagon");
    QFormLayout *form = new QFormLayout(group);

    hexRadiusSpin = new QDoubleSpinBox();
    hexRadiusSpin->setDecimals(1);
    hexRadiusSpin->setSingleStep(1.0);
    hexRadiusSpin->setValue(120.0);

    hexPointsSpin = new QSpinBox();
    hexPointsSpin->setRange(6, 2000);
    hexPointsSpin->setValue(48);

    hexRotationSpin = new QDoubleSpinBox();
    hexRotationSpin->setRange(-360.0, 360.0);
    hexRotationSpin->setDecimals(1);
    hexRotationSpin->setSingleStep(1.0);

    hexXShiftSpin = new QDoubleSpinBox();
    hexXShiftSpin->setDecimals(1);
    hexXShiftSpin->setSingleStep(1.0);

    hexYShiftSpin = new QDoubleSpinBox();
    hexYShiftSpin->setDecimals(1);
    hexYShiftSpin->setSingleStep(1.0);

    form->addRow("Radius:", hexRadiusSpin);
    form->addRow("No. of points:", hexPointsSpin);
    form->addRow("Rotation (deg):", hexRotationSpin);
    form->addRow("X shift:", hexXShiftSpin);
    form->addRow("Y shift:", hexYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createTwoSpotsPage() {
    QGroupBox *group = new QGroupBox("Two Spots");
    QFormLayout *form = new QFormLayout(group);

    twoSpotsDistanceSpin = new QDoubleSpinBox();
    twoSpotsDistanceSpin->setDecimals(1);
    twoSpotsDistanceSpin->setSingleStep(1.0);
    twoSpotsDistanceSpin->setValue(100.0);

    twoSpotsRotationSpin = new QDoubleSpinBox();
    twoSpotsRotationSpin->setRange(-360.0, 360.0);
    twoSpotsRotationSpin->setDecimals(1);
    twoSpotsRotationSpin->setSingleStep(1.0);

    twoSpotsXShiftSpin = new QDoubleSpinBox();
    twoSpotsXShiftSpin->setDecimals(1);
    twoSpotsXShiftSpin->setSingleStep(1.0);

    twoSpotsYShiftSpin = new QDoubleSpinBox();
    twoSpotsYShiftSpin->setDecimals(1);
    twoSpotsYShiftSpin->setSingleStep(1.0);

    form->addRow("Distance from center:", twoSpotsDistanceSpin);
    form->addRow("Rotation (deg):", twoSpotsRotationSpin);
    form->addRow("Center X shift:", twoSpotsXShiftSpin);
    form->addRow("Center Y shift:", twoSpotsYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createStarPage() {
    QGroupBox *group = new QGroupBox("Star");
    QFormLayout *form = new QFormLayout(group);

    starPointsSpin = new QSpinBox();
    starPointsSpin->setRange(3, 100);
    starPointsSpin->setValue(5);

    starOuterRadiusSpin = new QDoubleSpinBox();
    starOuterRadiusSpin->setDecimals(1);
    starOuterRadiusSpin->setSingleStep(1.0);
    starOuterRadiusSpin->setValue(120.0);

    starInnerRadiusSpin = new QDoubleSpinBox();
    starInnerRadiusSpin->setDecimals(1);
    starInnerRadiusSpin->setSingleStep(1.0);
    starInnerRadiusSpin->setValue(50.0);

    starTotalPointsSpin = new QSpinBox();
    starTotalPointsSpin->setRange(6, 2000);
    starTotalPointsSpin->setValue(50);

    starRotationSpin = new QDoubleSpinBox();
    starRotationSpin->setRange(-360.0, 360.0);
    starRotationSpin->setDecimals(1);
    starRotationSpin->setSingleStep(1.0);

    starXShiftSpin = new QDoubleSpinBox();
    starXShiftSpin->setDecimals(1);
    starXShiftSpin->setSingleStep(1.0);

    starYShiftSpin = new QDoubleSpinBox();
    starYShiftSpin->setDecimals(1);
    starYShiftSpin->setSingleStep(1.0);

    form->addRow("Points (Arms):", starPointsSpin);
    form->addRow("Outer radius:", starOuterRadiusSpin);
    form->addRow("Inner radius:", starInnerRadiusSpin);
    form->addRow("Total sampled points:", starTotalPointsSpin);
    form->addRow("Rotation (deg):", starRotationSpin);
    form->addRow("X shift:", starXShiftSpin);
    form->addRow("Y shift:", starYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createPlanetMoonPage() {
    QGroupBox *group = new QGroupBox("Planet & Moon");
    QFormLayout *form = new QFormLayout(group);

    pmPlanetRadiusSpin = new QDoubleSpinBox();
    pmPlanetRadiusSpin->setDecimals(1);
    pmPlanetRadiusSpin->setSingleStep(1.0);
    pmPlanetRadiusSpin->setValue(80.0);

    pmMoonRadiusSpin = new QDoubleSpinBox();
    pmMoonRadiusSpin->setDecimals(1);
    pmMoonRadiusSpin->setSingleStep(1.0);
    pmMoonRadiusSpin->setValue(20.0);

    pmDistanceSpin = new QDoubleSpinBox();
    pmDistanceSpin->setDecimals(1);
    pmDistanceSpin->setSingleStep(1.0);
    pmDistanceSpin->setValue(140.0);

    pmTotalPointsSpin = new QSpinBox();
    pmTotalPointsSpin->setRange(4, 2000);
    pmTotalPointsSpin->setValue(60);

    pmRotationSpin = new QDoubleSpinBox();
    pmRotationSpin->setRange(-360.0, 360.0);
    pmRotationSpin->setDecimals(1);
    pmRotationSpin->setSingleStep(1.0);

    pmXShiftSpin = new QDoubleSpinBox();
    pmXShiftSpin->setDecimals(1);
    pmXShiftSpin->setSingleStep(1.0);

    pmYShiftSpin = new QDoubleSpinBox();
    pmYShiftSpin->setDecimals(1);
    pmYShiftSpin->setSingleStep(1.0);

    form->addRow("Planet radius:", pmPlanetRadiusSpin);
    form->addRow("Moon radius:", pmMoonRadiusSpin);
    form->addRow("Moon distance:", pmDistanceSpin);
    form->addRow("Total sampled points:", pmTotalPointsSpin);
    form->addRow("Moon rotation (deg):", pmRotationSpin);
    form->addRow("Planet X shift:", pmXShiftSpin);
    form->addRow("Planet Y shift:", pmYShiftSpin);

    return group;
}

QWidget *PatternPresetsWidget::createGridPage() {
    QGroupBox *group = new QGroupBox("N*M Grid");
    QFormLayout *form = new QFormLayout(group);

    gridRowsSpin = new QSpinBox();
    gridRowsSpin->setRange(1, 100);
    gridRowsSpin->setValue(5);

    gridColsSpin = new QSpinBox();
    gridColsSpin->setRange(1, 100);
    gridColsSpin->setValue(5);

    gridRowSpacingSpin = new QDoubleSpinBox();
    gridRowSpacingSpin->setDecimals(1);
    gridRowSpacingSpin->setSingleStep(1.0);
    gridRowSpacingSpin->setValue(20.0);

    gridColSpacingSpin = new QDoubleSpinBox();
    gridColSpacingSpin->setDecimals(1);
    gridColSpacingSpin->setSingleStep(1.0);
    gridColSpacingSpin->setValue(20.0);

    gridCenterCheck = new QCheckBox("Center grid around origin");
    gridCenterCheck->setChecked(true);

    gridRotationSpin = new QDoubleSpinBox();
    gridRotationSpin->setRange(-360.0, 360.0);
    gridRotationSpin->setDecimals(1);
    gridRotationSpin->setSingleStep(1.0);

    gridXShiftSpin = new QDoubleSpinBox();
    gridXShiftSpin->setDecimals(1);
    gridXShiftSpin->setSingleStep(1.0);

    gridYShiftSpin = new QDoubleSpinBox();
    gridYShiftSpin->setDecimals(1);
    gridYShiftSpin->setSingleStep(1.0);

    form->addRow("Rows (N):", gridRowsSpin);
    form->addRow("Cols (M):", gridColsSpin);
    form->addRow("Row Spacing (Y px):", gridRowSpacingSpin);
    form->addRow("Col Spacing (X px):", gridColSpacingSpin);
    form->addRow("Alignment:", gridCenterCheck);
    form->addRow("Rotation (deg):", gridRotationSpin);
    form->addRow("X shift:", gridXShiftSpin);
    form->addRow("Y shift:", gridYShiftSpin);

    return group;
}

void PatternPresetsWidget::updateLimits() {
    const double halfWidth = static_cast<double>(cameraWidth) / 2.0;
    const double halfHeight = static_cast<double>(cameraHeight) / 2.0;
    const double minDimension = static_cast<double>(qMin(cameraWidth, cameraHeight));
    const double maxRadius = minDimension / 2.0;

    circleRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    triangleScaleSpin->setRange(1.0, qMax(1.0, maxRadius));
    hexRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    twoSpotsDistanceSpin->setRange(1.0, qMax(1.0, maxRadius));
    starOuterRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    starInnerRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    pmPlanetRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    pmMoonRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    pmDistanceSpin->setRange(1.0, qMax(1.0, maxRadius));

    // For row/col spacing, it's possible to want very small spacing
    gridRowSpacingSpin->setRange(0.1, qMax(1.0, static_cast<double>(cameraHeight)));
    gridColSpacingSpin->setRange(0.1, qMax(1.0, static_cast<double>(cameraWidth)));

    squareSizeSpin->setRange(1.0, qMax(1.0, minDimension));
    rectWidthSpin->setRange(1.0, qMax(1.0, static_cast<double>(cameraWidth)));
    rectHeightSpin->setRange(1.0, qMax(1.0, static_cast<double>(cameraHeight)));

    const auto applyShiftRange = [halfWidth, halfHeight](QDoubleSpinBox *xSpin, QDoubleSpinBox *ySpin) {
        xSpin->setRange(-halfWidth, halfWidth);
        ySpin->setRange(-halfHeight, halfHeight);
    };

    applyShiftRange(circleXShiftSpin, circleYShiftSpin);
    applyShiftRange(triangleXShiftSpin, triangleYShiftSpin);
    applyShiftRange(squareXShiftSpin, squareYShiftSpin);
    applyShiftRange(rectXShiftSpin, rectYShiftSpin);
    applyShiftRange(hexXShiftSpin, hexYShiftSpin);
    applyShiftRange(twoSpotsXShiftSpin, twoSpotsYShiftSpin);
    applyShiftRange(starXShiftSpin, starYShiftSpin);
    applyShiftRange(pmXShiftSpin, pmYShiftSpin);
    applyShiftRange(gridXShiftSpin, gridYShiftSpin);

    if (circleRadiusSpin->value() > maxRadius) {
        circleRadiusSpin->setValue(maxRadius);
    }
    if (triangleScaleSpin->value() > maxRadius) {
        triangleScaleSpin->setValue(maxRadius);
    }
    if (hexRadiusSpin->value() > maxRadius) {
        hexRadiusSpin->setValue(maxRadius);
    }
    if (twoSpotsDistanceSpin->value() > maxRadius) {
        twoSpotsDistanceSpin->setValue(maxRadius);
    }
    if (starOuterRadiusSpin->value() > maxRadius) {
        starOuterRadiusSpin->setValue(maxRadius);
    }
    if (starInnerRadiusSpin->value() > maxRadius) {
        starInnerRadiusSpin->setValue(maxRadius);
    }
    if (pmPlanetRadiusSpin->value() > maxRadius) {
        pmPlanetRadiusSpin->setValue(maxRadius);
    }
    if (pmMoonRadiusSpin->value() > maxRadius) {
        pmMoonRadiusSpin->setValue(maxRadius);
    }
    if (pmDistanceSpin->value() > maxRadius) {
        pmDistanceSpin->setValue(maxRadius);
    }
    if (squareSizeSpin->value() > minDimension) {
        squareSizeSpin->setValue(minDimension);
    }
    if (rectWidthSpin->value() > cameraWidth) {
        rectWidthSpin->setValue(cameraWidth);
    }
    if (rectHeightSpin->value() > cameraHeight) {
        rectHeightSpin->setValue(cameraHeight);
    }
}

QString PatternPresetsWidget::presetName(int index) const {
    switch (index) {
    case CircleIndex:
        return "Circle";
    case TriangleIndex:
        return "Triangle";
    case SquareIndex:
        return "Square";
    case RectangleIndex:
        return "Rectangle";
    case HexagonIndex:
        return "Hexagon";
    case TwoSpotsIndex:
        return "Two Spots";
    case StarIndex:
        return "Star";
    case PlanetAndMoonIndex:
        return "Planet & Moon";
    case GridIndex:
        return "N*M Grid";
    default:
        return "Pattern";
    }
}

