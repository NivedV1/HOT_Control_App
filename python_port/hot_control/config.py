from __future__ import annotations

import json
from pathlib import Path


DEFAULT_CONFIG = {
    "slm_width": 1920,
    "slm_height": 1080,
    "slm_pixel_size_um": 8.0,
    "slm_output_mode": "direct",
    "slm_window_id": 0,
    "slm_selected_monitor": 1,
    "cam_width": 1920,
    "cam_height": 1080,
    "cam_pixel_size_um": 5.0,
    "camera_backend": 0,
    "camera_preview_monitor": 1,
    "udp_bind_ip": "0.0.0.0",
    "udp_port": 9000,
    "camera_imaging_magnification": 1.0,
    "wavelength_nm": 1064.0,
    "focal_length_mm": 100.0,
    "iterations": 20,
    "wgs_relaxation": 0.5,
    "auto_run_gs": False,
    "auto_send_slm": False,
    "gs_starting_phase_mask": 0,
    "gs_compute_backend": 0,
    "ui_dark_mode": True,
    "ui_overlay_default": False,
}


def config_file(base_dir: Path) -> Path:
    return base_dir / "hardware_config.json"


def load_config(path: Path) -> dict:
    config = dict(DEFAULT_CONFIG)
    if not path.exists():
        return config
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        if isinstance(data, dict):
            config.update(data)
    except Exception:
        pass
    return config


def save_config(path: Path, config: dict) -> None:
    path.write_text(json.dumps(config, indent=2), encoding="utf-8")
