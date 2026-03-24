# Extending Built-In Patterns

## What This Guide Covers
This guide explains two developer workflows in the HOT Control App codebase:

1. Adding a new target-grid pattern to the `Pattern` tab shown in the main window controls below the grid.
2. Adding a new hologram generator pattern type to the standard phase-mask generator dialog using custom C++ code.

This is written for someone editing the source for the first time. It focuses on which files to change, which edits are required, and which extra updates are recommended to keep the feature complete and maintainable.

## Pattern Systems In This App
There are two different pattern systems:

- `Target-grid patterns`: generate trap point coordinates such as circle, triangle, star, or grid.
- `Hologram generator patterns`: generate a grayscale phase image pixel-by-pixel for the SLM.

They are separate features and are extended in different files.

## Adding A New Target-Grid Pattern

### Where This UI Lives
The UI that the user sees "below the grid" is part of the tabbed controls in the main window.

In [`src/ui/mainwindow.cpp`](../src/ui/mainwindow.cpp), the pattern editor is attached here:

```cpp
patternPresetsWidget = new PatternPresetsWidget(camWidth, camHeight);
targetModeTabs->addTab(patternPresetsWidget, "Pattern");
```

Most of the time, you do not need to edit `mainwindow.cpp` to add a new pattern preset. This file only matters if you want to move the entire pattern UI to a different place or change how tabs are arranged.

### Required Files
To add a new target-grid pattern, you usually edit these files:

- `src/ui/components/patternpresetswidget.h`
- `src/ui/components/patternpresetswidget.cpp`
- `src/core/patterngenerator.h`
- `src/core/patterngenerator.cpp`

### 1. Add A New Preset Index In `patternpresetswidget.h`
File: [`src/ui/components/patternpresetswidget.h`](../src/ui/components/patternpresetswidget.h)

This widget owns the pattern-selection UI. Add three things here:

1. A new value in `PresetIndex`
2. A declaration for a new page builder such as `createPentagonPage()`
3. Member variables for the controls used by the new pattern

Example shape of the change:

```cpp
enum PresetIndex {
    CircleIndex = 0,
    TriangleIndex,
    SquareIndex,
    RectangleIndex,
    HexagonIndex,
    TwoSpotsIndex,
    StarIndex,
    PlanetAndMoonIndex,
    GridIndex,
    PentagonIndex
};

QWidget *createPentagonPage();

QDoubleSpinBox *pentagonRadiusSpin;
QSpinBox *pentagonPointsSpin;
QDoubleSpinBox *pentagonRotationSpin;
QDoubleSpinBox *pentagonXShiftSpin;
QDoubleSpinBox *pentagonYShiftSpin;
```

Keep the enum order aligned with the combo-box order you will add in the `.cpp` file.

### 2. Add The New UI Page In `patternpresetswidget.cpp`
File: [`src/ui/components/patternpresetswidget.cpp`](../src/ui/components/patternpresetswidget.cpp)

This file is the main UI integration point for target-grid presets.

You need to update all of the following places:

#### A. Initialize The New Widget Pointers
In the constructor initializer list, initialize the new members to `nullptr`.

#### B. Add The Preset Name To The Drop-Down
Update `presetCombo->addItems(...)` so the new preset appears in the UI.

Example:

```cpp
presetCombo->addItems({
    "Circle",
    "Triangle",
    "Square",
    "Rectangle",
    "Hexagon",
    "Two Spots",
    "Star",
    "Planet & Moon",
    "N*M Grid",
    "Pentagon"
});
```

#### C. Add The Page To `optionsStack`
Add `optionsStack->addWidget(createPentagonPage());`

The order must match:

- `PresetIndex`
- combo-box items
- stacked-widget pages

If one of these is out of sync, the wrong page may be shown or the wrong pattern may be generated.

#### D. Build The Form Controls
Implement a page-creation method such as:

```cpp
QWidget *PatternPresetsWidget::createPentagonPage() {
    QGroupBox *group = new QGroupBox("Pentagon");
    QFormLayout *form = new QFormLayout(group);

    pentagonRadiusSpin = new QDoubleSpinBox();
    pentagonPointsSpin = new QSpinBox();
    pentagonRotationSpin = new QDoubleSpinBox();
    pentagonXShiftSpin = new QDoubleSpinBox();
    pentagonYShiftSpin = new QDoubleSpinBox();

    form->addRow("Radius:", pentagonRadiusSpin);
    form->addRow("No. of points:", pentagonPointsSpin);
    form->addRow("Rotation (deg):", pentagonRotationSpin);
    form->addRow("X shift:", pentagonXShiftSpin);
    form->addRow("Y shift:", pentagonYShiftSpin);

    return group;
}
```

The exact controls depend on your geometry. A preset can reuse existing ideas:

- radius + point count
- width + height
- spacing
- shift
- rotation

#### E. Read The New Controls In `onGenerateClicked()`
Add a new `case` inside `onGenerateClicked()` and populate a `PatternGenerator::PatternRequest`.

Example:

```cpp
case PentagonIndex:
    request.preset = PatternGenerator::Preset::Pentagon;
    request.radius = pentagonRadiusSpin->value();
    request.pointCount = pentagonPointsSpin->value();
    request.rotationDeg = pentagonRotationSpin->value();
    request.xShift = pentagonXShiftSpin->value();
    request.yShift = pentagonYShiftSpin->value();
    break;
```

This step is required. If you add controls but do not read them here, the UI will show the preset but nothing meaningful will be sent to the generator.

#### F. Update `presetName()` And `presetDetails()`
These are not strictly required for geometry generation, but they are strongly recommended.

- `presetName()` supplies the readable preset name used in status/summary text.
- `presetDetails()` records parameter values in a compact string useful for logs, debugging, and reproducibility.

Example:

```cpp
case PentagonIndex:
    return "Pentagon";
```

```cpp
case PentagonIndex:
    return QString("Pentagon(radius=%1, points=%2, rotation=%3, xShift=%4, yShift=%5)")
        .arg(pentagonRadiusSpin->value(), 0, 'f', 2)
        .arg(pentagonPointsSpin->value())
        .arg(pentagonRotationSpin->value(), 0, 'f', 2)
        .arg(pentagonXShiftSpin->value(), 0, 'f', 2)
        .arg(pentagonYShiftSpin->value(), 0, 'f', 2);
```

### 3. Add The New Preset Type In `patterngenerator.h`
File: [`src/core/patterngenerator.h`](../src/core/patterngenerator.h)

The generator backend is defined by:

- `PatternGenerator::Preset`
- `PatternGenerator::PatternRequest`

#### A. Add The Enum Value
Example:

```cpp
enum class Preset {
    Circle,
    Triangle,
    Square,
    Rectangle,
    Hexagon,
    TwoSpots,
    Star,
    PlanetAndMoon,
    Grid,
    Pentagon
};
```

#### B. Add Any New Request Fields
Only add new fields if your new pattern actually needs extra parameters that are not already covered.

Examples:

- `double innerRadius`
- `int sideCount`
- `double phaseOffset`

If your new shape can reuse existing fields like `radius`, `pointCount`, `rotationDeg`, `xShift`, and `yShift`, you do not need to grow `PatternRequest`.

### 4. Implement The Geometry In `patterngenerator.cpp`
File: [`src/core/patterngenerator.cpp`](../src/core/patterngenerator.cpp)

Add a new branch in `PatternGenerator::generate(const PatternRequest &request)`.

For polygon-like shapes, you can usually reuse existing helper functions such as:

- `makeRegularPolygon(...)`
- `samplePolygonEdges(...)`
- `applyShift(...)`

Example:

```cpp
case Preset::Pentagon: {
    const QVector<QPointF> vertices = makeRegularPolygon(5, request.radius, request.rotationDeg);
    points = samplePolygonEdges(vertices, count);
    return applyShift(points, shiftX, shiftY);
}
```

If the math is more complex, extract a helper function near the top of the file instead of writing a long branch directly inside the switch.

### Optional But Recommended Updates

#### Update `updateLimits()`
File: [`src/ui/components/patternpresetswidget.cpp`](../src/ui/components/patternpresetswidget.cpp)

If the new controls should respect camera dimensions, add them to `updateLimits()`.

Common examples:

- limit radius to half of the smaller camera dimension
- limit X shift to `[-halfWidth, +halfWidth]`
- limit Y shift to `[-halfHeight, +halfHeight]`

This is optional in the sense that the app can still compile without it, but it is recommended so the new preset behaves consistently with the existing ones.

#### Add Better Defaults
While creating the new page, set sensible defaults so first-time users can click `Generate` without tuning everything manually.

### Worked Example: Add A Pentagon Preset
For a basic pentagon preset, the minimum path is:

1. Add `PentagonIndex` in `PatternPresetsWidget::PresetIndex`
2. Add `"Pentagon"` to the preset combo box
3. Add `createPentagonPage()` to the stacked widget
4. Add radius/points/rotation/shift controls
5. Add `PatternGenerator::Preset::Pentagon`
6. Handle `PentagonIndex` in `onGenerateClicked()`
7. Handle `Preset::Pentagon` in `PatternGenerator::generate()`
8. Add `presetName()` and `presetDetails()` support
9. Optionally add `updateLimits()` handling for the new controls

Because the existing generator already has `makeRegularPolygon(...)`, the implementation can stay small and clean.

## Adding A New Hologram Generator Pattern Type

### Where This UI Lives
The standard phase-mask generator dialog is implemented in:

- [`src/ui/hologramdialog.h`](../src/ui/hologramdialog.h)
- [`src/ui/hologramdialog.cpp`](../src/ui/hologramdialog.cpp)

The main window opens it generically here in [`src/ui/mainwindow.cpp`](../src/ui/mainwindow.cpp):

```cpp
void MainWindow::openHologramGenerator() {
    HologramDialog dialog(slmWidth, slmHeight, this);
    connect(&dialog, &HologramDialog::maskReadyToLoad, this, &MainWindow::receiveHologram);
    connect(&dialog, &HologramDialog::sendToSLMRequested, this, &MainWindow::sendHologramToSLM);
    dialog.exec();
}
```

You usually do not need to change `mainwindow.cpp` when adding a new hologram pattern type. The dialog already supports opening any pattern type that `HologramDialog` exposes.

### Current Implementation Model
The hologram generator works differently from the trap-pattern system:

- `patternTypeCombo->currentIndex()` selects the current pattern type.
- `generatePattern()` iterates over every pixel of the SLM image.
- Each branch computes a phase value or grayscale output for that pixel.
- `updateParameterVisibility()` decides which controls are shown for the selected pattern.

This means the following pieces must stay synchronized:

1. combo-box item order
2. generation branch order in `generatePattern()`
3. visibility logic in `updateParameterVisibility()`

### Required Files
To add a new hologram generator pattern type, you usually edit:

- `src/ui/hologramdialog.h`
- `src/ui/hologramdialog.cpp`

### 1. Add New Parameter Widgets In `hologramdialog.h`
File: [`src/ui/hologramdialog.h`](../src/ui/hologramdialog.h)

If your pattern needs extra user-configurable parameters, add widget members here.

Example:

```cpp
QDoubleSpinBox *radialFrequencySpin;
QDoubleSpinBox *phaseOffsetSpin;
```

If your new pattern can reuse existing controls such as `periodSpin`, `angleSpin`, or `amplitudeSpin`, then no header change is needed.

### 2. Register The New Pattern In `hologramdialog.cpp`
File: [`src/ui/hologramdialog.cpp`](../src/ui/hologramdialog.cpp)

#### A. Add The Name To `patternTypeCombo`
Example:

```cpp
patternTypeCombo->addItems({
    "Blazed Grating (Prism)",
    "Binary Grating",
    "Fresnel Lens",
    "Axicon",
    "Vortex Beam",
    "Sinusoidal Grating",
    "Checkerboard",
    "Radial Sine"
});
```

This index becomes part of the dialog’s internal control flow, so order matters.

#### B. Create And Configure New Controls
If the pattern needs new inputs, instantiate them in the constructor:

```cpp
radialFrequencySpin = new ArrowDoubleSpinBox();
radialFrequencySpin->setRange(0.01, 10.0);
radialFrequencySpin->setValue(0.15);
```

Then add them to the form:

```cpp
form->addRow("Radial Frequency:", radialFrequencySpin);
```

#### C. Add The Generation Branch In `generatePattern()`
Inside the per-pixel loop, add a new `if` or `else if` branch for the new type.

Example shape:

```cpp
else if (type == 7) {
    const double radialFrequency = radialFrequencySpin->value();
    const double amplitude = amplitudeSpin->value();
    const double r = std::sqrt(cx * cx + cy * cy);

    double phi = M_PI + amplitude * M_PI * std::sin(radialFrequency * r);
    phi = std::fmod(phi, 2.0 * M_PI);
    if (phi < 0.0) phi += 2.0 * M_PI;
    row[x] = static_cast<uchar>((phi / (2.0 * M_PI)) * 255.0);
}
```

The existing code already shows the expected pattern:

- compute coordinates
- compute phase expression
- wrap to `[0, 2pi)`
- map phase to `0..255`

#### D. Update `updateParameterVisibility()`
Add the correct visibility rule for the new index.

Example:

```cpp
case 7: // Radial Sine
    amplitudeSpin->setVisible(true);
    radialFrequencySpin->setVisible(true);
    break;
```

If you forget this step, the pattern may work internally but the UI controls will remain hidden or unrelated controls may stay visible.

### Worked Example: Add A Radial Sine Pattern
Suppose you want a hologram pattern where phase varies sinusoidally with radius from the image center.

A clean implementation would be:

1. Add `"Radial Sine"` to `patternTypeCombo`
2. Add `radialFrequencySpin` in `hologramdialog.h`
3. Instantiate and configure `radialFrequencySpin` in the constructor
4. Add the control to the `QFormLayout`
5. Add a new `type == 7` branch in `generatePattern()`
6. Use existing centered coordinates `cx` and `cy`
7. Compute radius with `std::sqrt(cx * cx + cy * cy)`
8. Convert the sine-based phase into wrapped grayscale output
9. Update `updateParameterVisibility()` so only relevant controls appear

### Common Pixel-Generation Pattern
Most hologram generator types follow this structure:

```cpp
double phi = /* your formula */;
phi = std::fmod(phi, 2.0 * M_PI);
if (phi < 0.0) phi += 2.0 * M_PI;
row[x] = static_cast<uchar>((phi / (2.0 * M_PI)) * 255.0);
```

If your pattern is binary rather than continuous, you can directly assign `0`, `128`, or `255` style values instead.

## Common Mistakes

### Target-Grid Pattern Mistakes
- Adding a preset name to the combo box but forgetting to add the page to `optionsStack`
- Adding a page to `optionsStack` but forgetting to add the matching enum index
- Adding controls in the UI but not reading them in `onGenerateClicked()`
- Adding new fields to the UI but not adding them to `PatternRequest`
- Adding a new preset enum value but not handling it in `PatternGenerator::generate()`
- Forgetting to update `presetName()` and `presetDetails()`

### Hologram Generator Mistakes
- Adding a new combo-box item but not adding a matching branch in `generatePattern()`
- Using the wrong `type` index after inserting a new pattern in the middle of the list
- Adding new controls but forgetting to add them to the form layout
- Adding form controls but forgetting to show them in `updateParameterVisibility()`
- Showing controls in `updateParameterVisibility()` but forgetting to actually read them in the generation branch

## Quick Checklist

### New Target-Grid Pattern
- Add enum value to `PatternPresetsWidget::PresetIndex`
- Add UI widget members in `patternpresetswidget.h`
- Add combo-box item
- Add stacked-widget page
- Implement `create...Page()`
- Read parameters in `onGenerateClicked()`
- Add `PatternGenerator::Preset` enum value
- Add `PatternRequest` fields if needed
- Add `generate()` branch
- Update `presetName()`
- Update `presetDetails()`
- Optionally update `updateLimits()`

### New Hologram Pattern Type
- Add combo-box label
- Add widget members in `hologramdialog.h` if needed
- Instantiate/configure controls
- Add controls to the form
- Add generation branch in `generatePattern()`
- Add parameter visibility logic in `updateParameterVisibility()`

## Interfaces To Know
These are the main extension interfaces involved in this feature:

- `PatternGenerator::Preset`
- `PatternGenerator::PatternRequest`
- `PatternPresetsWidget::PresetIndex`
- `HologramDialog` constructor-built parameter widgets
- `HologramDialog::generatePattern()`
- `HologramDialog::updateParameterVisibility()`

## Final Notes
- Use the target-grid preset system when the output is a list of trap coordinates.
- Use the hologram dialog pattern system when the output is a full grayscale phase image.
- Try to reuse existing helper functions and parameter patterns before introducing brand-new structures.
- Keep UI order and switch-case order synchronized whenever selection depends on integer indices.
