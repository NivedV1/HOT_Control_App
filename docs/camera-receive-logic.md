# Camera Receive Logic

This document explains how camera frames are received and propagated when the app uses the `UdpStream` backend.

## Scope

Relevant files:

- `src/hardware/camera_stream.h`
- `src/hardware/camera_stream.cpp`
- `src/camera/cameramanager.h`
- `src/camera/cameramanager.cpp`

## High-Level Flow

1. `CameraManager::startCamera()` starts `CameraStream` with bind IP/port.
2. `CameraStream::receiveLoop()` runs on a dedicated thread and reads UDP packets.
3. Packets are validated and assembled into full grayscale frames by `frameID` + chunk metadata.
4. Completed frames emit `CameraStream::frameReady(const QImage&, quint32)` via queued Qt signal.
5. `CameraManager::onUdpFrameReceived(...)` updates cache/FPS/timeout state and emits `frameReady` for UI.
6. If recording is active, the frame is transformed/cropped and written with OpenCV `VideoWriter`.

## Startup and Threading

### `CameraManager` startup

`CameraManager::startCamera()` for `UdpStream`:

- Clears `lastUdpFrame`
- Resets `lastUdpFrameMs` and timeout flag
- Calls `udpStream->start(udpBindIp, udpPort)`
- Starts `udpHealthTimer` (500 ms) on success

`CameraStream::start(...)`:

- Stops any prior receiver thread
- Resets stream state and counters
- Sets `running = true`
- Spawns `receiveThread` -> `receiveLoop()`

### Thread model

- UDP receive/parsing happens in `CameraStream::receiveThread`.
- UI-safe signal emission uses `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`.
- `lastFrame` in `CameraStream` is protected by `stateMutex`.

## UDP Packet Formats

The receiver supports two packet headers.

### Current header (`PacketHeader`, packed)

Fields:

- `width`, `height`
- `codec` (`0` = raw grayscale, `1` = RLE-compressed grayscale)
- `frameID`
- `chunkIndex`, `totalChunks`
- `chunkOffset`, `chunkSize`
- `reserved0`, `reserved1` (must be `0`)

### Legacy header (`LegacyPacketHeaderV2`, packed)

Fields:

- `width`, `height`
- `frameID`
- `chunkIndex`, `totalChunks`
- `chunkOffset`, `chunkSize`

Legacy packets are treated as `codec = 0` (raw grayscale).

## Receive Loop Details

Inside `CameraStream::receiveLoop()`:

1. Windows socket setup (`WSAStartup`, UDP socket, receive buffer, receive timeout).
2. Bind to configured IP/port.
3. Loop while `running`:
   - `recvfrom(...)`
   - Parse current header if valid, otherwise try legacy header.
   - Reject malformed packets (bad chunk shape, size mismatch, bounds violations).

### Frame boundary and drop behavior

`handleFrameBoundary(...)` runs when a new `frameID` arrives (or no active frame exists).

- If previous frame is incomplete, it is dropped and drop counter increments.
- Status emits at most once per second for drop reports.
- Frame dimensions are validated (`1..8192` each dimension).
- `codec` must be `0` or `1`.
- Per-frame state is reset:
  - `expectedChunks`, `chunksReceived`
  - `chunkReceived[]`
  - `frameBuffer` sizing

### Chunk validation

For each packet, receiver checks:

- `header.codec == currentCodec`
- `header.totalChunks == expectedChunks`
- `chunkIndex < expectedChunks`
- `header.chunkSize == payloadSize`
- `chunkOffset + chunkSize` in valid bounds

If chunk not already received:

- Copy payload into `frameBuffer` at `chunkOffset`
- Mark `chunkReceived[chunkIndex] = true`
- Increment `chunksReceived`
- Track highest byte written (`frameDataBytes`)

### Frame completion

When `chunksReceived == expectedChunks`:

- `codec 0`: construct `QImage(..., QImage::Format_Grayscale8)` from raw bytes.
- `codec 1`: RLE decode (`decodeRleToRaw`) into `decodedBuffer`, then construct grayscale image.
- Copy image into `lastFrame` (mutex-protected).
- Increment complete-frame counter.
- Emit `frameReady(frameCopy, frameID)`.
- Clear active-frame state.

## Compression Logic (RLE)

`decodeRleToRaw(...)` expects byte pairs:

- `[runLength, value]`

Rules:

- `runLength` must be non-zero.
- Total expanded bytes must match `width * height` exactly.
- Odd-length encoded buffers are rejected.

On decode failure, receiver emits:

- `"UDP Stream: failed to decode compressed frame."`

## Integration in `CameraManager`

`CameraManager` connects:

- `CameraStream::frameReady` -> `CameraManager::onUdpFrameReceived`
- `CameraStream::statusMessage` -> `CameraManager::onUdpStatusMessage`

### `onUdpFrameReceived(const QImage&, quint32)`

- Ignores null frame.
- Increments FPS frame count.
- Caches `lastUdpFrame = frame.copy()`.
- Updates `lastUdpFrameMs` and clears timeout flag.
- If recording:
  - Apply optional flip/rotation transforms
  - Apply zoom crop (`applyZoomCrop`)
  - Convert grayscale -> BGR for OpenCV writer
  - Write frame and refresh recording timer
- Emits app-level `frameReady(lastUdpFrame)` for display/consumers.

### Health timer

`checkUdpHealth()` runs every 500 ms while stream is active.

- If no frame for > 2000 ms, emits once:
  - `"UDP Stream timeout: waiting for packets..."`
- Flag resets after next received frame.

## Operational Constraints and Notes

- Receiver implementation is currently Windows-only (`#ifdef _WIN32`).
- Max accepted dimensions: `8192 x 8192`.
- Max compressed payload buffer: `64 MB`.
- Max packet size read per UDP datagram: `4096` bytes.
- Duplicate chunks are ignored after first valid receipt.
- Out-of-order chunks are supported as long as metadata is valid.

## Stop/Cleanup

`CameraManager::stopCamera()` for UDP backend:

- Stops health timer
- Calls `udpStream->stop()` (joins receive thread)
- Stops FPS timer and recording writer
- Resets timeout tracking state

`CameraStream::stop()`:

- Sets `running = false`
- Joins receiver thread safely

## Quick Troubleshooting Map

- `"receiver not initialized"`: `udpStream` missing in `CameraManager`.
- `"bind failed"`: IP/port unavailable or invalid.
- Timeout message with no frames: sender not transmitting or network path issue.
- Drop-incomplete-frame status: packet loss/reordering exceeding frame boundary handling.
- Decode failures: sender/receiver codec mismatch or corrupted RLE stream.
