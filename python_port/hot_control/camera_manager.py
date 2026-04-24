from __future__ import annotations

import sys
from pathlib import Path

import cv2


class CameraManager:
    def __init__(self):
        self.capture = None
        self.camera_index = -1
        self.last_frame_bgr = None
        self.record_writer = None
        self.recording_path = None

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

        url_candidates = [
            f"udp://@{ip}:{p}",
            f"udp://{ip}:{p}",
        ]
        backends = [cv2.CAP_FFMPEG, cv2.CAP_ANY]

        for url in url_candidates:
            for backend in backends:
                cap = cv2.VideoCapture(url, backend)
                if cap.isOpened():
                    self.capture = cap
                    self.camera_index = -1
                    return True, f"UDP feed started on {ip}:{p}"
                cap.release()
        return False, f"Failed to open UDP feed on {ip}:{p}."

    def stop(self):
        self.stop_recording()
        if self.capture is not None:
            self.capture.release()
        self.capture = None
        self.camera_index = -1
        self.last_frame_bgr = None

    def read_frame(self):
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
