from __future__ import annotations

import socket
import struct
import sys
from pathlib import Path

import cv2
import numpy as np


class CameraManager:
    _XP_HEADER_V3_FORMAT = "<H H B B H I H H I I"
    _XP_HEADER_V3_SIZE = struct.calcsize(_XP_HEADER_V3_FORMAT)
    _XP_HEADER_FORMAT = "<H H I H H I I"
    _XP_HEADER_SIZE = struct.calcsize(_XP_HEADER_FORMAT)

    def __init__(self):
        self.capture = None
        self.camera_index = -1
        self.last_frame_bgr = None
        self.record_writer = None
        self.recording_path = None
        self.udp_socket = None
        self.udp_mode_custom = False
        self._udp_frame_id = -1
        self._udp_expected_chunks = 0
        self._udp_chunks_received = 0
        self._udp_width = 0
        self._udp_height = 0
        self._udp_frame_buffer = bytearray()
        self._udp_chunk_flags = []
        self._udp_last_complete_gray = None
        self._udp_codec = 0

    def _decode_rle_to_raw(self, encoded: bytes, expected_bytes: int):
        if expected_bytes <= 0:
            return None
        out = bytearray()
        i = 0
        n = len(encoded)
        while i + 1 < n:
            run = encoded[i]
            value = encoded[i + 1]
            if run == 0:
                return None
            if len(out) + run > expected_bytes:
                return None
            out.extend([value] * run)
            i += 2
        if i != n:
            return None
        if len(out) != expected_bytes:
            return None
        return out

    def discover_cameras(self, max_index=8):
        cameras = []
        for idx in range(max_index):
            cap = self._open_capture(idx)
            if cap is None:
                continue
            ok, frame = cap.read()
            if ok and frame is not None:
                h, w = frame.shape[:2]
                cameras.append({"index": idx, "name": f"Camera {idx} ({w}x{h})"})
            cap.release()
        return cameras

    def _open_capture(self, index, force_default_backend=False):
        backend = cv2.CAP_ANY
        if sys.platform.startswith("win") and not force_default_backend:
            backend = cv2.CAP_DSHOW
        cap = cv2.VideoCapture(index, backend)
        if not cap.isOpened():
            cap.release()
            return None
        return cap

    def start(self, index, use_default_backend=False):
        self.stop()
        cap = self._open_capture(index, force_default_backend=use_default_backend)
        if cap is None:
            return False, f"Failed to open camera index {index}."
        self.capture = cap
        self.camera_index = index
        return True, f"Camera {index} started."

    def start_udp(self, bind_ip, port):
        self.stop()
        ip = str(bind_ip).strip() if bind_ip is not None else "0.0.0.0"
        if not ip:
            ip = "0.0.0.0"
        try:
            p = int(port)
        except Exception:
            return False, "Invalid UDP port."
        if p < 1 or p > 65535:
            return False, "UDP port must be 1-65535."

        # 1) Try native XP Sender protocol receiver first (matches C++ app UDP receiver).
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 5 * 1024 * 1024)
            sock.bind((ip, p))
            sock.setblocking(False)
            self.udp_socket = sock
            self.udp_mode_custom = True
            self.camera_index = -1
            self._udp_frame_id = -1
            self._udp_expected_chunks = 0
            self._udp_chunks_received = 0
            self._udp_width = 0
            self._udp_height = 0
            self._udp_frame_buffer = bytearray()
            self._udp_chunk_flags = []
            self._udp_last_complete_gray = None
            self._udp_codec = 0
            return True, f"UDP receiver bound on {ip}:{p} (XP Sender protocol)."
        except Exception:
            self.udp_socket = None
            self.udp_mode_custom = False

        # 2) Fallback: attempt FFmpeg/OpenCV UDP stream URLs.
        url_candidates = [
            f"udp://@:{p}",
            f"udp://@{ip}:{p}",
            f"udp://0.0.0.0:{p}",
            f"udp://{ip}:{p}",
            f"udp://127.0.0.1:{p}",
        ]
        backends = [cv2.CAP_FFMPEG, cv2.CAP_GSTREAMER, cv2.CAP_ANY]

        for url in url_candidates:
            for backend in backends:
                cap = cv2.VideoCapture(url, backend)
                if cap.isOpened():
                    self.capture = cap
                    self.camera_index = -1
                    return True, f"UDP feed started on {ip}:{p} (OpenCV backend)."
                cap.release()
        return False, f"Failed to open UDP feed on {ip}:{p}."

    def stop(self):
        self.stop_recording()
        if self.capture is not None:
            self.capture.release()
        self.capture = None
        if self.udp_socket is not None:
            try:
                self.udp_socket.close()
            except Exception:
                pass
        self.udp_socket = None
        self.udp_mode_custom = False
        self.camera_index = -1
        self.last_frame_bgr = None
        self._udp_frame_id = -1
        self._udp_expected_chunks = 0
        self._udp_chunks_received = 0
        self._udp_width = 0
        self._udp_height = 0
        self._udp_frame_buffer = bytearray()
        self._udp_chunk_flags = []
        self._udp_last_complete_gray = None
        self._udp_codec = 0

    def _read_frame_udp_custom(self):
        if self.udp_socket is None:
            return None

        # Drain available datagrams quickly without blocking UI.
        for _ in range(512):
            try:
                data, _addr = self.udp_socket.recvfrom(65535)
            except BlockingIOError:
                break
            except OSError:
                break

            if len(data) < self._XP_HEADER_SIZE:
                continue
            parsed = False
            codec = 0
            width = height = frame_id = chunk_index = total_chunks = chunk_offset = chunk_size = 0
            header_size = 0

            if len(data) >= self._XP_HEADER_V3_SIZE:
                try:
                    cand = struct.unpack(self._XP_HEADER_V3_FORMAT, data[: self._XP_HEADER_V3_SIZE])
                    (
                        width,
                        height,
                        codec,
                        reserved0,
                        reserved1,
                        frame_id,
                        chunk_index,
                        total_chunks,
                        chunk_offset,
                        chunk_size,
                    ) = cand
                    codec_valid = codec in (0, 1)
                    reserved_valid = reserved0 == 0 and reserved1 == 0
                    chunk_shape_valid = total_chunks > 0 and chunk_index < total_chunks
                    if codec_valid and reserved_valid and chunk_shape_valid:
                        parsed = True
                        header_size = self._XP_HEADER_V3_SIZE
                except Exception:
                    parsed = False

            if not parsed:
                try:
                    (
                        width,
                        height,
                        frame_id,
                        chunk_index,
                        total_chunks,
                        chunk_offset,
                        chunk_size,
                    ) = struct.unpack(self._XP_HEADER_FORMAT, data[: self._XP_HEADER_SIZE])
                    codec = 0
                    if total_chunks <= 0 or chunk_index >= total_chunks:
                        continue
                    parsed = True
                    header_size = self._XP_HEADER_SIZE
                except Exception:
                    continue

            payload = data[header_size:]

            if width <= 0 or height <= 0:
                continue
            frame_bytes = int(width) * int(height)
            if frame_bytes <= 0:
                continue
            if total_chunks <= 0 or chunk_index >= total_chunks:
                continue
            if chunk_size != len(payload):
                continue
            if chunk_offset + chunk_size > frame_bytes:
                continue

            # New frame boundary.
            if frame_id != self._udp_frame_id:
                self._udp_frame_id = int(frame_id)
                self._udp_expected_chunks = int(total_chunks)
                self._udp_chunks_received = 0
                self._udp_width = int(width)
                self._udp_height = int(height)
                self._udp_codec = int(codec)
                if self._udp_codec == 0:
                    if len(self._udp_frame_buffer) != frame_bytes:
                        self._udp_frame_buffer = bytearray(frame_bytes)
                else:
                    self._udp_frame_buffer = bytearray()
                self._udp_chunk_flags = [False] * self._udp_expected_chunks

            if total_chunks != self._udp_expected_chunks:
                continue
            if int(codec) != self._udp_codec:
                continue
            if chunk_index < 0 or chunk_index >= len(self._udp_chunk_flags):
                continue

            if not self._udp_chunk_flags[chunk_index]:
                if self._udp_codec == 0:
                    if chunk_offset + chunk_size > len(self._udp_frame_buffer):
                        continue
                    self._udp_frame_buffer[chunk_offset : chunk_offset + chunk_size] = payload
                else:
                    end = int(chunk_offset + chunk_size)
                    if end > len(self._udp_frame_buffer):
                        self._udp_frame_buffer.extend(b"\x00" * (end - len(self._udp_frame_buffer)))
                    self._udp_frame_buffer[chunk_offset:end] = payload
                self._udp_chunk_flags[chunk_index] = True
                self._udp_chunks_received += 1

                if self._udp_chunks_received >= self._udp_expected_chunks:
                    try:
                        if self._udp_codec == 0:
                            raw = self._udp_frame_buffer
                        else:
                            decoded = self._decode_rle_to_raw(bytes(self._udp_frame_buffer), frame_bytes)
                            if decoded is None:
                                self._udp_last_complete_gray = None
                                continue
                            raw = decoded
                        gray = np.frombuffer(raw, dtype=np.uint8).reshape((self._udp_height, self._udp_width))
                        self._udp_last_complete_gray = np.array(gray, copy=True)
                    except Exception:
                        self._udp_last_complete_gray = None

        if self._udp_last_complete_gray is None:
            return self.last_frame_bgr
        frame_bgr = cv2.cvtColor(self._udp_last_complete_gray, cv2.COLOR_GRAY2BGR)
        self.last_frame_bgr = frame_bgr
        return frame_bgr

    def read_frame(self):
        if self.udp_mode_custom:
            return self._read_frame_udp_custom()
        if self.capture is None:
            return None
        ok, frame = self.capture.read()
        if not ok or frame is None:
            return None
        self.last_frame_bgr = frame
        return frame

    def save_snapshot(self, path):
        if self.last_frame_bgr is None:
            return False
        p = Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        return bool(cv2.imwrite(str(p), self.last_frame_bgr))

    def start_recording(self, path, fps=30):
        if self.last_frame_bgr is None:
            return False, "No camera frame available to start recording."
        h, w = self.last_frame_bgr.shape[:2]
        p = Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        fourcc = cv2.VideoWriter_fourcc(*"mp4v")
        writer = cv2.VideoWriter(str(p), fourcc, float(max(1, fps)), (w, h))
        if not writer.isOpened():
            writer.release()
            return False, "Failed to start video writer."
        self.record_writer = writer
        self.recording_path = str(p)
        return True, f"Recording started: {p.name}"

    def write_record_frame(self, frame_bgr):
        if self.record_writer is not None and frame_bgr is not None:
            self.record_writer.write(frame_bgr)

    def stop_recording(self):
        if self.record_writer is not None:
            self.record_writer.release()
        self.record_writer = None
        self.recording_path = None

    def is_recording(self):
        return self.record_writer is not None
