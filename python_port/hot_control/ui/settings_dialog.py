from __future__ import annotations

from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFormLayout,
    QLineEdit,
    QSpinBox,
    QVBoxLayout,
)


class SettingsDialog(QDialog):
    def __init__(self, parent, config, selected_monitor):
        super().__init__(parent)
        self._config = dict(config)
        self._selected_monitor = int(selected_monitor)
        self.setWindowTitle("Hardware Settings")
        self.resize(480, 420)

        root = QVBoxLayout(self)
        form = QFormLayout()
        root.addLayout(form)

        self.slm_width = QSpinBox()
        self.slm_width.setRange(64, 4096)
        self.slm_width.setValue(int(config.get("slm_width", 1920)))
        self.slm_height = QSpinBox()
        self.slm_height.setRange(64, 4096)
        self.slm_height.setValue(int(config.get("slm_height", 1080)))
        self.slm_px = QDoubleSpinBox()
        self.slm_px.setRange(0.1, 50.0)
        self.slm_px.setValue(float(config.get("slm_pixel_size_um", 8.0)))

        self.cam_width = QSpinBox()
        self.cam_width.setRange(64, 4096)
        self.cam_width.setValue(int(config.get("cam_width", 1920)))
        self.cam_height = QSpinBox()
        self.cam_height.setRange(64, 4096)
        self.cam_height.setValue(int(config.get("cam_height", 1080)))
        self.cam_px = QDoubleSpinBox()
        self.cam_px.setRange(0.1, 50.0)
        self.cam_px.setValue(float(config.get("cam_pixel_size_um", 5.0)))
        self.cam_mag = QDoubleSpinBox()
        self.cam_mag.setRange(0.01, 20.0)
        self.cam_mag.setValue(float(config.get("camera_imaging_magnification", 1.0)))

        self.wave = QDoubleSpinBox()
        self.wave.setRange(100.0, 3000.0)
        self.wave.setValue(float(config.get("wavelength_nm", 1064.0)))
        self.focal = QDoubleSpinBox()
        self.focal.setRange(1.0, 2000.0)
        self.focal.setValue(float(config.get("focal_length_mm", 100.0)))

        self.camera_backend = QComboBox()
        self.camera_backend.addItems(["Default", "DirectShow (Windows)", "UDP Feed"])
        backend_idx = int(config.get("camera_backend", 0))
        if backend_idx < 0 or backend_idx > 2:
            backend_idx = 0
        self.camera_backend.setCurrentIndex(backend_idx)

        self.udp_ip = QLineEdit(str(config.get("udp_bind_ip", "0.0.0.0")))
        self.udp_port = QSpinBox()
        self.udp_port.setRange(1, 65535)
        self.udp_port.setValue(int(config.get("udp_port", 9000)))

        self.monitor = QSpinBox()
        self.monitor.setRange(1, 16)
        self.monitor.setValue(self._selected_monitor)

        form.addRow("SLM Width", self.slm_width)
        form.addRow("SLM Height", self.slm_height)
        form.addRow("SLM Pixel (um)", self.slm_px)
        form.addRow("Camera Width", self.cam_width)
        form.addRow("Camera Height", self.cam_height)
        form.addRow("Camera Pixel (um)", self.cam_px)
        form.addRow("Camera Magnification", self.cam_mag)
        form.addRow("Wavelength (nm)", self.wave)
        form.addRow("Focal Length (mm)", self.focal)
        form.addRow("Camera Backend", self.camera_backend)
        form.addRow("UDP Bind IP", self.udp_ip)
        form.addRow("UDP Port", self.udp_port)
        form.addRow("SLM Monitor Number", self.monitor)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

    def result_payload(self):
        out = dict(self._config)
        out["slm_width"] = int(self.slm_width.value())
        out["slm_height"] = int(self.slm_height.value())
        out["slm_pixel_size_um"] = float(self.slm_px.value())
        out["cam_width"] = int(self.cam_width.value())
        out["cam_height"] = int(self.cam_height.value())
        out["cam_pixel_size_um"] = float(self.cam_px.value())
        out["camera_imaging_magnification"] = float(self.cam_mag.value())
        out["wavelength_nm"] = float(self.wave.value())
        out["focal_length_mm"] = float(self.focal.value())
        out["camera_backend"] = int(self.camera_backend.currentIndex())
        out["udp_bind_ip"] = self.udp_ip.text().strip() or "0.0.0.0"
        out["udp_port"] = int(self.udp_port.value())
        return {"config": out, "selected_monitor": int(self.monitor.value())}
