# RME Algorithm Overview In HOT Control App

## Scope
This note explains how the new Random Mask Encoding (RME) algorithm is implemented and used in the app.

Current state:
- `Gerchberg-Saxton` is implemented.
- `Weighted GS` is still a placeholder.
- `Random Mask Encoding (Paper)` is implemented as a third algorithm option.

Relevant files:
- `src/core/algorithms/rme_algorithm.h`
- `src/core/algorithms/rme_algorithm.cpp`
- `src/ui/mainwindow.cpp`
- `CMakeLists.txt`

## Paper Context
The implementation follows the non-iterative idea from:
- "Fast generation of holographic optical tweezers by random mask encoding of Fourier components"
- Optics Express 14(6):2101-2107 (2006), DOI: `10.1364/OE.14.002101`

Practical idea:
- split the SLM plane into disjoint random pixel subsets
- assign each subset to one target trap
- encode the linear phase ramp of that trap only on its subset
- combine everything directly, without GS iterations

## High-Level Flow
At runtime, RME generation does:

1. Validate optical and hardware settings.
2. Map camera-centered target points to Fourier-grid offsets.
3. Deduplicate valid targets by offset.
4. Build a deterministic random partition of SLM pixels into near-equal subsets (one subset per trap).
5. For each pixel, apply the linear phase ramp of the subset's assigned trap.
6. Wrap phase into `[0, 2pi)` and convert to 8-bit grayscale `[0,255]`.

There is no iterative loop and no backend switching in v1.

## Public API
The RME API is isolated in `namespace RMEAlgorithm`:

- `RMETargetPoint`
  - `xCamPx`
  - `yCamPx`
- `RMEConfig`
  - SLM dimensions and pixel size
  - camera dimensions and pixel size
  - camera imaging magnification
  - wavelength
  - Fourier lens focal length
- `RMEResult`
  - success/error
  - `phaseMask8Bit`
  - `wrappedPhaseRad`
  - target usage diagnostics
- entry point:
  - `runRandomMaskEncoding(const RMEConfig&, const QVector<RMETargetPoint>&)`

## Mapping: Camera Space To Fourier Offsets
RME uses the same physical mapping model as GS.

Computed terms:
- `slmDx = slmPixelSizeUm * 1e-6`
- `effectiveCamPixelSizeUm = camPixelSizeUm / cameraImagingMagnification`
- `camDx = effectiveCamPixelSizeUm * 1e-6`
- `wavelength = wavelengthNm * 1e-9`
- `focalLength = focalLengthMm * 1e-3`
- `focalDx = (wavelength * focalLength) / (slmWidth * slmDx)`
- `focalDy = (wavelength * focalLength) / (slmHeight * slmDy)`

For each target:
- convert camera pixels to physical displacement
- convert displacement to FFT-grid offsets by rounding
- reject points outside camera FOV or mapped outside SLM Fourier bounds

RME then deduplicates mapped `(fftOffsetX, fftOffsetY)` pairs so repeated points at the same offset become one trap channel.

## Deterministic Random Partition
RME v1 uses a stable seed to keep output reproducible for the same input setup.

Seed inputs:
- SLM width and height
- number of valid traps
- each trap offset pair

Process:
- create a full list of SLM pixel indices
- shuffle once using deterministic `std::mt19937` seed
- split shuffled list into `N` near-equal groups (`N = trap count`)
  - each trap gets `floor(P/N)` pixels
  - first `P % N` traps get one extra pixel

This yields a disjoint partition:
- no overlap across trap subsets
- every SLM pixel assigned exactly once

## Phase Synthesis
If pixel `(x, y)` belongs to trap `k` with offsets `(dx_k, dy_k)`, phase is:

`phi(x,y) = 2pi * (dx_k * x / W - dy_k * y / H)`

where:
- `W` is SLM width
- `H` is SLM height

Then:
- wrap `phi` to `[0, 2pi)`
- store wrapped radian value in `wrappedPhaseRad`
- map to grayscale with `(wrapped / 2pi) * 255`

## UI Integration
Algorithm selector now has three entries:
- `Gerchberg-Saxton`
- `Weighted GS`
- `Random Mask Encoding (Paper)`

Behavior:
- RME runs through `MainWindow::generateAlgorithmMask(...)` when selected.
- RME participates in:
  - manual generate
  - auto-run on point edits (when enabled)
  - pre-send regeneration in `onSendToSlmRequested(...)`
- Iteration and relaxation controls are hidden for RME because the method is non-iterative.

Animation/Python:
- v1 playback remains GS-only.
- if non-GS is selected, animation generation reports that Weighted GS and RME playback are not supported.

## Differences From GS
RME vs GS:
- RME is direct, non-iterative, and CPU-only in v1.
- GS is iterative and supports CPU/OpenCL/CUDA backends.
- RME does not use source amplitude constraints in v1.
- GS enforces both source-plane and target-plane constraints iteratively.

## v1 Limitations
- No RME OpenCL/CUDA path yet.
- No per-trap weighting yet (uniform trap power target via near-equal partition only).
- No RME animation or Python playback mode.
- Weighted GS is still not implemented.

