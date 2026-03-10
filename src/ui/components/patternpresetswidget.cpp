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
      hexYShiftSpin(nullptr) {

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *helpText = new QLabel("Pick a preset, tune parameters, and click Generate to replace current grid points.");
    helpText->setWordWrap(true);
    helpText->setStyleSheet("font-size: 11px;");

    QHBoxLayout *presetRow = new QHBoxLayout();
    QLabel *presetLabel = new QLabel("Preset:");
    presetCombo = new QComboBox();
    presetCombo->addItems({"Circle", "Triangle", "Square", "Rectangle", "Hexagon"});
    presetRow->addWidget(presetLabel);
    presetRow->addWidget(presetCombo, 1);

    optionsStack = new QStackedWidget();
    optionsStack->addWidget(createCirclePage());
    optionsStack->addWidget(createTrianglePage());
    optionsStack->addWidget(createSquarePage());
    optionsStack->addWidget(createRectanglePage());
    optionsStack->addWidget(createHexagonPage());

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
    default:
        request.preset = PatternGenerator::Preset::Hexagon;
        request.radius = hexRadiusSpin->value();
        request.pointCount = hexPointsSpin->value();
        request.rotationDeg = hexRotationSpin->value();
        request.xShift = hexXShiftSpin->value();
        request.yShift = hexYShiftSpin->value();
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

void PatternPresetsWidget::updateLimits() {
    const double halfWidth = static_cast<double>(cameraWidth) / 2.0;
    const double halfHeight = static_cast<double>(cameraHeight) / 2.0;
    const double minDimension = static_cast<double>(qMin(cameraWidth, cameraHeight));
    const double maxRadius = minDimension / 2.0;

    circleRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));
    triangleScaleSpin->setRange(1.0, qMax(1.0, maxRadius));
    hexRadiusSpin->setRange(1.0, qMax(1.0, maxRadius));

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

    if (circleRadiusSpin->value() > maxRadius) {
        circleRadiusSpin->setValue(maxRadius);
    }
    if (triangleScaleSpin->value() > maxRadius) {
        triangleScaleSpin->setValue(maxRadius);
    }
    if (hexRadiusSpin->value() > maxRadius) {
        hexRadiusSpin->setValue(maxRadius);
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
    default:
        return "Pattern";
    }
}

