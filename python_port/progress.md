# HOT Control Python Port Progress

## Step 1 - Scaffold and core compute
- [x] Create python_port project structure.
- [x] Add dependency list and run entrypoint.
- [x] Build parity UI shell with tabs: Manual, Pattern, Image, Python, Camera(stub), Animation(stub).
- [x] Port core algorithms (GS/WGS/RME) in plain Python modules.
- [x] Add target generation (manual table, pattern presets, image threshold import, python script frames).
- [x] Add correction-mask compose and save pipeline.
- [x] Keep persistence for hardware/optical settings in `hardware_config.json`.
- [x] Reshape UI to match C++ structure: menus + 3 monitor panels + tools row + 3-column control area.

## Step 2 - Camera integration (next)
- [x] OpenCV camera discovery/start/stop.
- [x] UDP camera feed option (config-driven backend mode).
- [x] Live preview with target overlay and basic pixel readout.
- [x] Snapshot and recording controls.

## Step 3 - SLM output routing (next)
- [x] Direct-screen output window routing (fullscreen, monitor-selectable).
- [x] Vendor DLL bridge parity (`Image_Control.dll`) wrapper.
- [x] Send/Clear behavior parity.

## Step 4 - Advanced parity (next)
- [x] Animation timeline controls and real-time mode.
- [x] Media sequence playback (video/folder to mask frames).
- [x] Additional UI parity and workflow polish.

## Step 5 - Settings and persistence
- [x] Hardware settings dialog with optical/camera/SLM fields.
- [x] Persist and re-apply selected hardware runtime settings.
- [x] Integrate monitor selection persistence with direct-screen output.

## Step 6 - Polish and validation
- [x] Unify SLM routing across manual send, animation playback, and phase-media playback.
- [x] Add output mode selection (`Direct Screen` / `Image_Control.dll`) with status feedback.
- [x] Run compile/import smoke checks after parity steps.

## Step 7 - Parity QA pass
- [x] Create side-by-side workflow checklist report.
- [x] Validate parity status against key workflows.
- [x] Track remaining open gaps for hardware verification and regression tests.

## Step 8 - UI mismatch patch pass
- [x] Add interactive target-pane point editing (add/move/remove + hover readout).
- [x] Add Python-tab parity controls (realtime, trap selector, send/stop/reset, save/load script).
- [x] Add camera preview monitor output window controls (show/hide monitor, save/record, FPS/pixel readout).
- [x] Add phase-media player dialog UI (preview, slider, frame info, play/stop).
- [x] Add grid enlarge/restore toggle in monitor header.
