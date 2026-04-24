from __future__ import annotations

import ctypes
from pathlib import Path
import sys

import numpy as np


class SlmDllOutput:
    def __init__(self, candidate_paths):
        self._candidate_paths = [Path(p) for p in candidate_paths]
        self._dll = None
        self._window_settings = None
        self._window_array_to_display = None
        self._window_term = None
        self.loaded_path = None
        self.last_error = None

    def is_loaded(self):
        return self._dll is not None

    def load(self):
        self.last_error = None
        if not sys.platform.startswith("win"):
            self.last_error = "Image_Control.dll output is only supported on Windows."
            return False
        if self._dll is not None:
            return True

        existing = [p for p in self._candidate_paths if p.exists()]
        if not existing:
            self.last_error = "Image_Control.dll not found in expected paths."
            return False

        for path in existing:
            try:
                dll = ctypes.WinDLL(str(path))
                ws = dll.Window_Settings
                wa = dll.Window_Array_to_Display
                wt = dll.Window_Term
                ws.argtypes = [ctypes.c_int32, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32]
                ws.restype = None
                wa.argtypes = [
                    ctypes.POINTER(ctypes.c_uint8),
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                    ctypes.c_int32,
                ]
                wa.restype = None
                wt.argtypes = [ctypes.c_int32]
                wt.restype = None
                self._dll = dll
                self._window_settings = ws
                self._window_array_to_display = wa
                self._window_term = wt
                self.loaded_path = str(path)
                return True
            except Exception as exc:  # noqa: BLE001
                self.last_error = f"Failed loading {path.name}: {exc}"
        return False

    def send_mask(self, mask_u8, monitor_number, window_id=0):
        if not self.load():
            return False, self.last_error or "DLL not loaded."
        arr = np.asarray(mask_u8, dtype=np.uint8)
        if arr.ndim != 2:
            return False, "Mask must be grayscale 2D array."
        arr = np.ascontiguousarray(arr)
        h, w = arr.shape
        ptr = arr.ctypes.data_as(ctypes.POINTER(ctypes.c_uint8))
        try:
            self._window_settings(int(monitor_number), int(window_id), 0, 0)
            self._window_array_to_display(ptr, int(w), int(h), int(window_id), int(w * h))
            return True, "Mask sent through Image_Control.dll"
        except Exception as exc:  # noqa: BLE001
            return False, f"DLL send failed: {exc}"

    def clear(self, window_id=0):
        if not self.load():
            return False, self.last_error or "DLL not loaded."
        try:
            self._window_term(int(window_id))
            return True, "DLL output terminated."
        except Exception as exc:  # noqa: BLE001
            return False, f"DLL clear failed: {exc}"
