from __future__ import annotations

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSpinBox,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)


class SettingsDialog(QDialog):
    settings_applied = Signal(dict)

    def __init__(self, parent, config, selected_monitor):
        super().__init__(parent)
        self._config = dict(config)
        self._selected_monitor = int(selected_monitor)
        self.setWindowTitle("Hardware Settings")
        self.resize(760, 980)

        root = QVBoxLayout(self)
        tabs = QTabWidget()
        root.addWidget(tabs, 1)

        hardware_tab = QWidget()
        hardware_layout = QVBoxLayout(hardware_tab)
        tabs.addTab(hardware_tab, "Hardware")

        ui_tab = QWidget()
        ui_layout = QVBoxLayout(ui_tab)
        tabs.addTab(ui_tab, "UI Settings")

        slm_group = QGroupBox("SLM Parameters")
        slm_form = QFormLayout(slm_group)
        slm_form.setLabelAlignment(
            slm_form.labelAlignment()
        )
        self.slm_width = QSpinBox()
        self.slm_width.setRange(64, 4096)
        self.slm_width.setValue(int(config.get("slm_width", 1920)))
        self.slm_height = QSpinBox()
        self.slm_height.setRange(64, 4096)
        self.slm_height.setValue(int(config.get("slm_height", 1080)))
        self.slm_px = QDoubleSpinBox()
        self.slm_px.setRange(0.1, 50.0)
        self.slm_px.setDecimals(2)
        self.slm_px.setSuffix(" um")
        self.slm_px.setValue(float(config.get("slm_pixel_size_um", 8.0)))

        self.slm_output_mode = QComboBox()
        self.slm_output_mode.addItem("Direct Screen", "direct")
        self.slm_output_mode.addItem("DLL", "dll")
        out_mode = str(config.get("slm_output_mode", "direct")).lower().strip()
        self.slm_output_mode.setCurrentIndex(1 if out_mode == "dll" else 0)

        self.auto_run_gs = QCheckBox()
        self.auto_run_gs.setChecked(bool(config.get("auto_run_gs", False)))
        self.auto_send_slm = QCheckBox()
        self.auto_send_slm.setChecked(bool(config.get("auto_send_slm", False)))

        self.starting_phase = QComboBox()
        self.starting_phase.addItems(["Checkerboard Pattern", "Binary Grating", "Random Phase"])
        self.starting_phase.setCurrentIndex(int(config.get("gs_starting_phase_mask", 0)))

        self.gs_compute_backend = QComboBox()
        self.gs_compute_backend.addItems(["Auto", "CPU", "OpenCL", "CUDA"])
        backend_idx = int(config.get("gs_compute_backend", 0))
        if backend_idx < 0 or backend_idx > 3:
            backend_idx = 0
        self.gs_compute_backend.setCurrentIndex(backend_idx)

        self.opencl_device = QComboBox()
        self.opencl_device.addItem("[P1:D0] Placeholder OpenCL Device")
        self.opencl_device.setCurrentIndex(0)

        self.cuda_device = QComboBox()
        self.cuda_device.addItem("No CUDA devices detected")
        self.cuda_device.setCurrentIndex(0)

        slm_form.addRow("SLM Width (pixels):", self.slm_width)
        slm_form.addRow("SLM Height (pixels):", self.slm_height)
        slm_form.addRow("SLM Pixel Size:", self.slm_px)
        slm_form.addRow("SLM Output Mode:", self.slm_output_mode)
        slm_form.addRow("Auto-run GS:", self.auto_run_gs)
        slm_form.addRow("Auto-send SLM:", self.auto_send_slm)
        slm_form.addRow("Starting Phase Mask:", self.starting_phase)
        slm_form.addRow("GS Compute Backend:", self.gs_compute_backend)
        slm_form.addRow("OpenCL Device:", self.opencl_device)
        slm_form.addRow("CUDA Device:", self.cuda_device)

        camera_group = QGroupBox("Camera Parameters")
        camera_form = QFormLayout(camera_group)
        self.camera_backend = QComboBox()
        self.camera_backend.addItems(["Qt Native", "OpenCV DirectShow", "UDP Stream (Ethernet)"])
        backend_idx = int(config.get("camera_backend", 0))
        if backend_idx < 0 or backend_idx > 2:
            backend_idx = 0
        self.camera_backend.setCurrentIndex(backend_idx)

        self.cam_width = QSpinBox()
        self.cam_width.setRange(64, 4096)
        self.cam_width.setValue(int(config.get("cam_width", 1920)))
        self.cam_height = QSpinBox()
        self.cam_height.setRange(64, 4096)
        self.cam_height.setValue(int(config.get("cam_height", 1080)))
        self.cam_px = QDoubleSpinBox()
        self.cam_px.setRange(0.1, 50.0)
        self.cam_px.setDecimals(2)
        self.cam_px.setSuffix(" um")
        self.cam_px.setValue(float(config.get("cam_pixel_size_um", 5.0)))
        self.udp_ip = QLineEdit(str(config.get("udp_bind_ip", "0.0.0.0")))
        self.udp_port = QSpinBox()
        self.udp_port.setRange(1, 65535)
        self.udp_port.setValue(int(config.get("udp_port", 9000)))

        camera_form.addRow("Camera Engine:", self.camera_backend)
        camera_form.addRow("Camera Width (px):", self.cam_width)
        camera_form.addRow("Camera Height (px):", self.cam_height)
        camera_form.addRow("Camera Pixel Size:", self.cam_px)
        camera_form.addRow("UDP Bind IP:", self.udp_ip)
        camera_form.addRow("UDP Port:", self.udp_port)

        optical_group = QGroupBox("Optical Setup")
        optical_form = QFormLayout(optical_group)
        self.wave = QDoubleSpinBox()
        self.wave.setRange(100.0, 3000.0)
        self.wave.setDecimals(2)
        self.wave.setSuffix(" nm")
        self.wave.setValue(float(config.get("wavelength_nm", 1064.0)))
        self.focal = QDoubleSpinBox()
        self.focal.setRange(0.1, 2000.0)
        self.focal.setDecimals(3)
        self.focal.setSuffix(" mm")
        self.focal.setValue(float(config.get("focal_length_mm", 100.0)))
        self.cam_mag = QDoubleSpinBox()
        self.cam_mag.setRange(0.01, 100.0)
        self.cam_mag.setDecimals(3)
        self.cam_mag.setValue(float(config.get("camera_imaging_magnification", 1.0)))
        self.monitor = QSpinBox()
        self.monitor.setRange(1, 16)
        self.monitor.setValue(self._selected_monitor)

        optical_form.addRow("Laser Wavelength:", self.wave)
        optical_form.addRow("Fourier Lens Focal Length (physical):", self.focal)
        optical_form.addRow("Camera Imaging Magnification:", self.cam_mag)
        optical_form.addRow("SLM Monitor Number:", self.monitor)

        hardware_layout.addWidget(slm_group)
        hardware_layout.addWidget(camera_group)
        hardware_layout.addWidget(optical_group)
        hardware_layout.addWidget(QLabel("Monitor target is selected from Tools > Select Monitor"))
        hardware_layout.addStretch()

        self.ui_dark_mode = QCheckBox("Dark Theme")
        self.ui_dark_mode.setChecked(bool(config.get("ui_dark_mode", True)))
        self.ui_show_overlay = QCheckBox("Enable Overlay Target by Default")
        self.ui_show_overlay.setChecked(bool(config.get("ui_overlay_default", False)))
        ui_layout.addWidget(self.ui_dark_mode)
        ui_layout.addWidget(self.ui_show_overlay)
        ui_layout.addStretch()

        btn_row = QHBoxLayout()
        btn_row.addStretch()
        self.save_btn = QPushButton("Save")
        self.cancel_btn = QPushButton("Cancel")
        self.apply_btn = QPushButton("Apply")
        self.save_btn.clicked.connect(self._on_save)
        self.cancel_btn.clicked.connect(self.reject)
        self.apply_btn.clicked.connect(self._on_apply)
        btn_row.addWidget(self.save_btn)
        btn_row.addWidget(self.cancel_btn)
        btn_row.addWidget(self.apply_btn)
        root.addLayout(btn_row)

    def _collect_payload(self):
        out = dict(self._config)
        out["slm_width"] = int(self.slm_width.value())
        out["slm_height"] = int(self.slm_height.value())
        out["slm_pixel_size_um"] = float(self.slm_px.value())
        out["slm_output_mode"] = str(self.slm_output_mode.currentData())
        out["auto_run_gs"] = bool(self.auto_run_gs.isChecked())
        out["auto_send_slm"] = bool(self.auto_send_slm.isChecked())
        out["gs_starting_phase_mask"] = int(self.starting_phase.currentIndex())
        out["gs_compute_backend"] = int(self.gs_compute_backend.currentIndex())
        out["cam_width"] = int(self.cam_width.value())
        out["cam_height"] = int(self.cam_height.value())
        out["cam_pixel_size_um"] = float(self.cam_px.value())
        out["camera_imaging_magnification"] = float(self.cam_mag.value())
        out["wavelength_nm"] = float(self.wave.value())
        out["focal_length_mm"] = float(self.focal.value())
        out["camera_backend"] = int(self.camera_backend.currentIndex())
        out["udp_bind_ip"] = self.udp_ip.text().strip() or "0.0.0.0"
        out["udp_port"] = int(self.udp_port.value())
        out["ui_dark_mode"] = bool(self.ui_dark_mode.isChecked())
        out["ui_overlay_default"] = bool(self.ui_show_overlay.isChecked())
        return {"config": out, "selected_monitor": int(self.monitor.value())}

    def _on_apply(self):
        self.settings_applied.emit(self._collect_payload())

    def _on_save(self):
        self.accept()

    def result_payload(self):
        return self._collect_payload()
