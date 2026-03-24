# GS Algorithm Overview In HOT Control App

## Scope
This note explains how Gerchberg-Saxton (GS) is currently implemented and used in the app.

Important current-state note:
- `Gerchberg-Saxton` is implemented.
- `Weighted GS` is exposed in the UI, but it is not implemented yet.
- If the user selects `Weighted GS`, the app shows a message and does not generate a mask.

Relevant files:
- `src/ui/mainwindow.cpp`
- `src/core/algorithms/gs_algorithm.h`
- `src/core/algorithms/gs_algorithm.cpp`
- `src/core/algorithms/gs_algorithm_cuda.h`
- `src/core/algorithms/gs_algorithm_cuda.cu`

## High-Level Flow
At a high level, the app turns user-selected target points into an SLM phase mask:

1. The user places target points in camera-centered coordinates.
2. `MainWindow` gathers optical settings, source intensity, backend choice, and iteration count.
3. The app maps target points from camera space into Fourier-plane pixels on the SLM-sized working grid.
4. The GS solver iteratively enforces:
   - the source amplitude constraint in the SLM plane
   - the target amplitude constraint in the focal plane
5. The final phase is wrapped into `[0, 2pi)` and converted to an 8-bit grayscale image.
6. That grayscale image becomes the phase mask preview and can be sent to the SLM.

## Where The Algorithm Is Triggered
The main UI entry point is `MainWindow::generateAlgorithmMask()` in `src/ui/mainwindow.cpp`.

That function:
- rejects `Weighted GS` because it is not implemented yet
- collects target points from `gridPointData`
- builds a `GSAlgorithm::GSConfig`
- chooses the source amplitude map
- calls `GSAlgorithm::runGerchbergSaxton(...)`
- stores the returned `phaseMask8Bit` into `currentMask`

The same GS path is also reused for:
- auto-run updates
- pre-send SLM generation
- animation frame generation through `runGsForTargetPoints(...)`

This means static patterns, animation frames, and Python-generated point sets all converge on the same GS backend.

## Inputs To GS
The public API is declared in `src/core/algorithms/gs_algorithm.h`.

### `GSConfig`
This structure carries the physical and compute settings:
- SLM width and height
- SLM pixel size in micrometers
- camera width and height
- camera pixel size in micrometers
- laser wavelength
- Fourier lens focal length
- iteration count
- starting phase mask mode
- compute backend preference
- selected OpenCL or CUDA device index

### `sourceAmplitude`
This is the amplitude constraint in the SLM plane.

The app uses:
- `sourceIntensityMap` if the user supplied one
- otherwise a default Gaussian profile built by `GSAlgorithm::buildGaussianSourceAmplitude(...)`

Despite the variable name, this is treated as amplitude data in the solver.

### `targets`
Targets are passed as `GSTargetPoint` values:
- `(0, 0)` is camera center
- `+x` is right
- `+y` is up

These are not directly used as SLM pixels. They are first converted into Fourier-plane sample positions.

## Preprocessing Before Iteration
The preprocessing work happens in `prepareGsData(...)` in `src/core/algorithms/gs_algorithm.cpp`.

### 1. Validation
The function first checks:
- valid SLM and camera dimensions
- positive optical parameters
- positive iteration count
- source amplitude size matches `slmWidth * slmHeight`
- at least one target exists

If any of these fail, GS stops early with an error message.

### 2. Camera-To-Fourier Mapping
The app converts camera-space target points into the FFT grid used by the solver.

It computes:
- SLM pixel pitch in meters
- camera pixel pitch in meters
- wavelength in meters
- focal length in meters

Then it derives the focal-plane sampling interval:

`focalDx = (wavelength * focalLength) / (slmWidth * slmDx)`

`focalDy = (wavelength * focalLength) / (slmHeight * slmDy)`

Conceptually, this is the Fourier relationship between the SLM plane and the focal plane.

For each target point:
- convert camera pixels to physical displacement
- divide by focal-plane pixel pitch
- round to an FFT-grid offset
- shift relative to the SLM-grid center

Targets can be discarded in two places:
- outside the camera field of view
- outside the SLM-sized Fourier grid after mapping

The surviving targets are written into `prepared.targetAmplitude` as sparse `1.0f` pixels.

So the target amplitude constraint used by GS is currently a binary spot map:
- target pixels = `1`
- all other pixels = `0`

### 3. Initial Phase Guess
The app supports three initial phase patterns:
- `Checkerboard`
- `BinaryGrating`
- `RandomPhase`

This becomes the starting phase estimate on the SLM plane.

## Core GS Iteration
The CPU implementation lives in `runGerchbergSaxtonCpuInternal(...)`.

### Mathematical Idea
GS alternates between two domains:
- SLM plane
- focal plane

At each iteration:
1. Build a complex SLM field from source amplitude and current phase.
2. Forward transform to the focal plane using a Fourier transform.
3. Keep focal-plane phase, but replace focal-plane amplitude with the target amplitude.
4. Inverse transform back to the SLM plane.
5. Keep only the returned SLM phase.
6. Repeat.

In compact form:

1. `U_slm = A_source * exp(i * phi_slm)`
2. `U_focal = FFT(U_slm)`
3. `U_focal' = A_target * exp(i * angle(U_focal))`
4. `U_slm' = IFFT(U_focal')`
5. `phi_slm = angle(U_slm')`

This is the standard Gerchberg-Saxton phase retrieval loop.

### CPU Implementation Details
On the CPU path:
- OpenCV complex matrices are used
- `ifftshift` is applied before each DFT
- `fftshift` is applied after each DFT
- the solver uses `cv::dft(...)`
- phase is extracted with `atan2(imag, real)`

The shift operations are important because the app wants a center-origin interpretation of the target pattern rather than raw FFT corner ordering.

## OpenCL Backend
The OpenCL implementation lives in `runGerchbergSaxtonOpenClInternal(...)`.

It is not a different algorithm. It is the same GS loop implemented with:
- `cv::UMat`
- OpenCV OpenCL execution
- OpenCL device selection and binding

The steps are the same:
- create complex SLM field from amplitude and phase
- forward DFT
- impose target amplitude
- inverse DFT
- extract updated phase

If the requested OpenCL device cannot be used, the code attempts to bind a default OpenCV OpenCL device before giving up.

## CUDA Backend
The CUDA implementation is routed through:
- `src/core/algorithms/gs_algorithm_cuda.h`
- `src/core/algorithms/gs_algorithm_cuda.cu`

`runGerchbergSaxton(...)` converts Qt containers into native vectors and calls:

`CudaBackend::runGerchbergSaxtonCudaNative(...)`

The CUDA path follows the same conceptual GS pipeline, but executes the heavy work on the GPU. If CUDA succeeds, the returned phase array is converted into the same final image format as the CPU/OpenCL paths.

## Backend Selection And Fallback
The public dispatcher is `GSAlgorithm::runGerchbergSaxton(...)`.

Backend behavior:
- `CPU`: always run CPU implementation directly
- `OpenCL`: try OpenCL, then fall back to CPU if needed
- `CUDA`: try CUDA, then fall back to CPU if needed
- `Auto`: try CUDA first, then OpenCL, then CPU

The result includes:
- which backend was actually used
- backend/device info text
- whether fallback occurred
- why fallback happened

That fallback information is surfaced in the status bar.

## Output Phase Mask
After the iterations finish, `populateResultPhaseImage(...)` builds the final output.

For each pixel:
- wrap phase into `[0, 2pi)`
- store the wrapped value in `wrappedPhaseRad`
- scale phase linearly to grayscale `0..255`

So the displayed and exported mask is an 8-bit grayscale encoding of wrapped phase.

## How The App Uses The Result
Back in `MainWindow::generateAlgorithmMask()`:
- `result.phaseMask8Bit` becomes `currentMask`
- the preview is updated
- a status message reports iterations, valid targets, source choice, backend used, and fallback details
- the mask may be auto-sent to the SLM if that option is enabled

The helper `runGsForTargetPoints(...)` uses the same mechanism for animation frames.

## Relationship To Python Scripting
Python scripting does not replace the GS solver.

Instead, Python scripts generate target point sets or frame sequences. Those points are then fed into the same GS pipeline as manually placed targets. This keeps one consistent hologram-generation path across:
- manual target placement
- preset/generated patterns
- animation playback
- Python-authored patterns

## Current Limitations
### Weighted GS Is Not Implemented
The UI offers:
- `Gerchberg-Saxton`
- `Weighted GS`

But in `MainWindow::generateAlgorithmMask()` the non-GS branch immediately shows:
- `"Weighted GS is not implemented yet."`

Also, `src/core/algorithms/wgs_algorithm.h` is currently empty.

So today the app is effectively a GS-only implementation with a placeholder UI option for WGS.

### Binary Target Amplitude
The target constraint is currently a sparse binary amplitude map. There is no per-trap weighting in the implemented path.

### Environment Sensitivity
Backend behavior depends on:
- CUDA toolkit availability and compatibility
- OpenCL device availability through OpenCV
- runtime-deployed Python and dependency packaging

The fallback logic is helpful, but it also means runtime behavior can vary across machines.

## Practical Summary
If you want the shortest mental model of the current app:

- the user defines trap points in camera coordinates
- the app maps those points onto a Fourier grid
- GS iteratively solves for an SLM phase-only hologram that produces those focal-plane spots
- the app converts the solved phase into an 8-bit grayscale mask
- that mask is previewed, saved, animated, or sent to the SLM

And at the moment:
- GS works
- backend auto-fallback works
- Python feeds target points into GS
- Weighted GS is still a future feature
