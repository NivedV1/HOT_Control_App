### Hologram Generator: Implement Missing Phase Pattern Logic

### Summary
Implement generation logic for the newly added pattern names so every `patternTypeCombo` entry produces a valid phase mask. Keep a single phase-output convention: compute phase in radians, wrap to `[0, 2pi)`, and map to grayscale `[0, 255]`.

### Implementation Changes
- In `C:\Users\NIVED\Desktop\HOT_Control_App\src\ui\hologramdialog.cpp`, extend `generatePattern()` to handle all pattern indices:
1. `Fresnel Lens` (`type == 2`): centered quadratic phase  
   `phi = (pi / f) * (cx^2 + cy^2)` with `f = focalLengthSpin->value()` (converging default).
2. `Axicon` (`type == 3`): replace cone-angle behavior with radial-period behavior  
   `r = sqrt(cx^2 + cy^2)`, `phi = 2pi * (r / radialPeriod)`.
3. `Vortex Beam` (`type == 4`): azimuthal phase  
   `phi = l * atan2(cy, cx)` with `l = topologicalChargeSpin->value()` (positive-only).
4. `Sinusoidal Grating` (`type == 5`): phase-depth sinusoid along rotated projection  
   `s = sin(2pi * proj / period)`, `phi = pi + amplitude * pi * s`, with `amplitude in [0,1]`.
- Keep existing implementations for `Blazed`, `Binary`, and `Checkerboard`.
- Add a small helper (local lambda or private method) to wrap radians to `[0, 2pi)` and convert to `uchar`.
- Replace Axicon UI control from `Cone Angle` to `Radial Period`:
1. Rename/control swap in dialog setup and labels.
2. Use range `2..1000 px`, default `50`.
3. Update visibility logic so Axicon shows radial period (not cone angle).
- In `C:\Users\NIVED\Desktop\HOT_Control_App\src\ui\hologramdialog.h`, update member declarations accordingly (remove/replace cone-angle spin box with radial-period spin box).

### Public Interfaces / Types
- No external/public API changes outside `HologramDialog` internals.
- Internal UI parameter set changes for Axicon: `Cone Angle` control is replaced by `Radial Period (pixels)`.

### Test Plan
1. Open generator and iterate all 7 pattern types; `Generate` should produce non-null image each time.
2. Parameter visibility checks:
   - Fresnel: only focal length visible.
   - Axicon: only radial period visible.
   - Vortex: only topological charge visible.
   - Sinusoidal: period + angle + amplitude visible.
3. Visual sanity checks:
   - Fresnel: concentric quadratic-like rings.
   - Axicon: evenly spaced conical/ring-like phase ramp.
   - Vortex: angular phase winding around center singularity.
   - Sinusoidal: smooth stripe modulation with amplitude depth change.
4. Regression checks:
   - Blazed/Binary/Checkerboard output unchanged.
   - Save/Load-to-main/Send-to-SLM still work for all generated patterns.

### Assumptions
- Phase output stays 8-bit grayscale and is interpreted by downstream SLM path exactly as current masks.
- Vortex remains positive-charge only (no chirality toggle).
- Fresnel polarity default is converging behavior (positive focal-length form).
