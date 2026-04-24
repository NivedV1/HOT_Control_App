# HOT Control Python Port - Parity QA Checklist

Date: 2026-04-24

## 1. Core startup/UI shell
- [x] Main window opens with 3 monitor panes (Target, Phase, Camera)
- [x] Menus present: File, View, Tools, Help
- [x] Tabs present: Manual, Pattern, Image, Camera, Animation, Python

## 2. Target workflow parity
- [x] Manual points add/clear/apply
- [x] Pattern generation into shared point list
- [x] Image threshold import to points
- [x] Python script `build_pattern` / `build_frames` support

## 3. Algorithm pipeline parity
- [x] GS mask generation
- [x] WGS mask generation
- [x] RME mask generation
- [x] Correction compose and phase mask save

## 4. Camera workflow parity
- [x] Camera discovery list refresh
- [x] Start/stop local camera feed
- [x] UDP feed option (backend mode `UDP Feed`)
- [x] Live preview overlay and pixel readout
- [x] Snapshot save
- [x] Video recording toggle

## 5. SLM output parity
- [x] Direct-screen fullscreen output path
- [x] Monitor selection and persistence
- [x] DLL wrapper (`Image_Control.dll`) load + send + term
- [x] Output mode selector (`Direct Screen` / `Image_Control.dll`)
- [x] Unified send behavior for manual send / animation / media playback
- [x] Clear behavior with correction-only retention

## 6. Animation/media parity
- [x] Animation generate/play/stop/reset
- [x] Realtime and precomputed playback behavior
- [x] Media source load: image / video / folder sequence
- [x] Media playback timer + optional send-to-SLM

## 7. Settings/persistence parity
- [x] Hardware settings dialog implemented
- [x] Optical/camera/SLM runtime fields persisted
- [x] Camera backend + UDP bind info persisted

## Open gaps (remaining)
- [ ] Full runtime parity verification on physical lab hardware (camera + SLM device path)
- [ ] Automated regression tests for main workflows
- [ ] Optional advanced C++ features not yet mirrored 1:1 (e.g., every minor dialog/action detail)
