# Separate WGS Module With Its Own CPU, OpenCL, and CUDA Paths

## Summary
Implement WGS as a fully separate algorithm stack so the existing GS code stays untouched.

V1 architecture:
- keep `src/core/algorithms/gs_algorithm.h` and `src/core/algorithms/gs_algorithm.cpp` unchanged
- turn the empty `src/core/algorithms/wgs_algorithm.h` into the public WGS API
- add `src/core/algorithms/wgs_algorithm.cpp` for CPU + OpenCL WGS
- add `src/core/algorithms/wgs_algorithm_cuda.h` and `src/core/algorithms/wgs_algorithm_cuda.cu` for CUDA WGS
- update `CMakeLists.txt` to build the new WGS sources alongside the existing GS sources
- keep the live camera-feedback loop in `MainWindow`, but call only the new `WGSAlgorithm` module when the user selects `Weighted GS`

## Public Interfaces

### New WGS types in `wgs_algorithm.h`
Define a separate `namespace WGSAlgorithm` with its own types, not shared by editing GS headers:
- `WGSStartingPhaseMask`
- `WGSComputeBackend`
- `WGSComputeBackendUsed`
- `WGSTargetPoint`
  - `pointId`
  - `xCamPx`
  - `yCamPx`
  - `targetWeight`
- `WGSConfig`
  - same optical/grid/backend fields GS needs
  - inner `iterations`
- `WGSResult`
  - success/error
  - generated mask
  - backend used/info/fallback
  - target-count diagnostics
- `WgsSpotDebugRow`
  - pass index
  - spot id
  - centered coordinates
  - transformed-frame coordinates
  - target weight
  - measured max intensity
  - calculated feedback weight
  - effective target amplitude

### New WGS API
Expose separate WGS entry points such as:
- `runWeightedGerchbergSaxton(...)`
- `updateSpotWeightsFromMeasurements(...)`

The split should be:
- solver call returns a weighted phase mask for the current per-spot amplitudes
- feedback-update call takes measured intensities and returns the next calculated per-spot weights plus debug rows

## Implementation Changes

### Separate solver stack
- In `src/core/algorithms/wgs_algorithm.cpp`, implement a standalone weighted version of:
  - camera-to-Fourier mapping
  - initial phase generation
  - CPU weighted GS loop
  - OpenCL weighted GS loop
  - backend fallback handling
- In `src/core/algorithms/wgs_algorithm_cuda.cu`, implement the CUDA WGS pipeline separately from GS CUDA.
- Keep duplication intentional for v1 so existing GS behavior and files are not disturbed.
- Use per-trap target amplitudes derived from `sqrt(target intensity weight)` instead of binary target pixels.

### Main window flow
- In `src/ui/mainwindow.cpp` and `src/ui/mainwindow.h`:
  - add `isWeightedGsSelected()`
  - branch `generateAlgorithmMask()` into GS vs WGS paths
  - keep GS path calling existing `GSAlgorithm`
  - add a separate WGS run path that:
    1. snapshots current static traps and target weights
    2. runs `WGSAlgorithm::runWeightedGerchbergSaxton(...)`
    3. sends the mask to the SLM
    4. waits the configured settle delay
    5. measures the transformed camera frame
    6. calls `WGSAlgorithm::updateSpotWeightsFromMeasurements(...)`
    7. repeats for the configured feedback-pass count
- Keep animation/Python on GS only in v1 and reject WGS there with a clear message.

### Camera-view matching
- Add one shared helper that transforms the latest camera frame exactly like the on-screen feed:
  - same flip X / flip Y
  - same rotation
  - same order as preview rendering
- Measure on that transformed full-resolution frame, before overlay graphics are painted.
- Map trap coordinates into the transformed frame so sampling matches what the user sees on screen.

### UI changes
- Add `Target Weight` as a fourth table column for WGS mode.
- Store per-trap metadata instead of only `QPointF`.
- Show WGS controls under algorithm settings:
  - `GS Iterations`
  - `Feedback Passes`
  - `Search Radius (px)`
  - `Feedback Gain`
  - `Settle Delay (ms)` default `300`
- Change button text/context appropriately when WGS is selected.

### CSV debug output
- For every WGS run, create one timestamped CSV in the app directory, alongside the existing debug log style.
- Write one row per spot per feedback pass.
- CSV columns:
  - `run_id`
  - `pass_index`
  - `spot_id`
  - `x_cam_px`
  - `y_cam_px`
  - `x_analysis_px`
  - `y_analysis_px`
  - `target_weight`
  - `target_fraction`
  - `measured_max_intensity`
  - `measured_fraction`
  - `calculated_weight`
  - `effective_target_amplitude`
- Flush rows as the run proceeds so partial data survives aborted runs.
- Report the CSV path in the status bar when the run finishes or aborts.

## Test Plan
- Build with CPU-only configuration and verify separate WGS sources compile without touching GS behavior.
- Build with OpenCL enabled and verify WGS OpenCL path runs from the new module.
- Build with CUDA enabled and verify WGS CUDA path is compiled from the new CUDA files.
- Manual UI checks:
  - WGS shows `Target Weight` and WGS settings
  - new/manual/preset points default to weight `1.0`
  - weights persist across moves and GS/WGS toggles
- Manual live checks:
  - equal target weights converge toward equal measured intensity
  - unequal target weights converge toward the requested ratio
  - search-circle max detection works for slightly offset spots
  - flip/rotation combinations match the displayed feed
- CSV checks:
  - one CSV file per WGS run
  - row count equals `spots x measured passes`
  - each row includes spot number, coordinates, target weight, measured max intensity, and calculated weight
- Regression checks:
  - plain GS output path still behaves exactly as before
  - animation/Python remain GS-only and fail cleanly under WGS selection

## Assumptions
- Existing GS source files remain functionally and textually untouched unless a trivial include/build-list change outside them is required.
- Separate WGS code duplication is acceptable in v1 to protect the existing GS implementation.
- WGS is limited to static trap sets in v1.
- The transformed camera view, not the raw frame and not the scaled QLabel pixmap, is the source of truth for measurement.
