# Direct Algorithm Code

This folder contains UI-free C++ helpers for running the same phase-mask algorithms used by the HOT Control App UI. These files are intentionally not added to the app target; they are direct algorithm code you can copy, include, or wire into another program later.

## Files

- `direct_gs_algorithm.h`: all GS variables and public direct-call functions.
- `direct_gs_algorithm.cpp`: full standalone CPU GS implementation, including `fftshift`, `ifftshift`, forward Fourier transform, inverse Fourier transform, phase extraction, and grayscale phase-mask output.
- `direct_rme_algorithm.h`: all Random Mask Encoding variables and public direct-call functions.
- `direct_rme_algorithm.cpp`: full standalone Random Mask Encoding implementation for point targets.
- `direct_algorithms.h`: convenience include for both algorithms.
- `direct_gs_algorithm.py`: simple standalone Python GS script for grid-click point targets with forward/inverse Fourier transforms.
- `direct_rme_algorithm.py`: simple standalone Python Random Mask Encoding script.
- `direct_algorithms.py`: convenience imports for the simple Python functions.

These files do not include or call the app UI or the app core algorithm files.

For C++, copy this folder and link against Qt and OpenCV.

For Python, install:

```bash
pip install numpy pillow
```

## Point Target Flow

`generatePhaseMaskFromPoints(...)` mirrors the normal trap-point UI path:

1. Accept camera-centered points where `(0, 0)` is camera center, `+x` is right, and `+y` is up.
2. Map them to the SLM-sized Fourier grid using the same optical formulas as the UI.
3. Run the standalone point-based GS loop.

## Minimal Example

```cpp
#include "algorithm_direct/direct_gs_algorithm.h"

QImage input("target.png");

AlgorithmDirect::DirectGsVariables vars;
vars.slmWidth = 1920;
vars.slmHeight = 1080;
vars.slmPixelSizeUm = 8.0;
vars.camWidth = 1920;
vars.camHeight = 1080;
vars.camPixelSizeUm = 5.0;
vars.cameraImagingMagnification = 1.0;
vars.wavelengthNm = 1064.0;
vars.focalLengthMm = 100.0;
vars.iterations = 20;
vars.startingPhaseMask = AlgorithmDirect::DirectGsStartingPhaseMask::Checkerboard;

AlgorithmDirect::DirectGsResult result =
    AlgorithmDirect::generatePhaseMaskFromImage(input, vars);

if (result.success) {
    result.phaseMask8Bit.save("phase_mask.png");
}
```

If `vars.sourceAmplitude` is empty, the code builds the same default Gaussian source as the UI: beam waist `min(slmWidth, slmHeight) / 6`.

## Random Mask Encoding Example

```cpp
#include "algorithm_direct/direct_rme_algorithm.h"

AlgorithmDirect::DirectRmeVariables vars;
vars.slmWidth = 1920;
vars.slmHeight = 1080;
vars.slmPixelSizeUm = 8.0;
vars.camWidth = 1920;
vars.camHeight = 1080;
vars.camPixelSizeUm = 5.0;
vars.cameraImagingMagnification = 1.0;
vars.wavelengthNm = 1064.0;
vars.focalLengthMm = 100.0;

QVector<QPointF> targets;
targets.append(QPointF(0.0, 0.0));
targets.append(QPointF(100.0, 0.0));

AlgorithmDirect::DirectRmeResult result =
    AlgorithmDirect::generateRandomMaskEncodingPhaseMask(targets, vars);

if (result.success) {
    result.phaseMask8Bit.save("rme_phase_mask.png");
}
```

## Python GS Example

```python
from direct_gs_algorithm import (
    default_settings,
    generate_from_points,
    save_mask,
)

settings = default_settings()
settings["iterations"] = 20
settings["starting_phase"] = "checkerboard"

grid_click_points = [(0.0, 0.0), (100.0, 0.0)]
result = generate_from_points(grid_click_points, settings)
save_mask("phase_mask.png", result["phase_mask_8bit"])
```

## Python Random Mask Encoding Example

```python
from direct_rme_algorithm import (
    default_settings,
    random_mask_encoding,
    save_mask,
)

settings = default_settings()

result = random_mask_encoding([(0.0, 0.0), (100.0, 0.0)], settings)
save_mask("rme_phase_mask.png", result["phase_mask_8bit"])
```
