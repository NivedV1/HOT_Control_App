from __future__ import annotations

from datetime import datetime
from pathlib import Path
import time

import cv2
import numpy as np
from PIL import Image
from PySide6.QtCore import QEvent, QPoint, QRect, QTimer, Qt
from PySide6.QtGui import QAction, QGuiApplication, QImage, QPixmap
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QLineEdit,
    QPushButton,
    QRadioButton,
    QSlider,
    QSpinBox,
    QStackedWidget,
    QTableWidget,
    QTableWidgetItem,
    QTabWidget,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from ..algorithms import generate_phase_mask
from ..camera_manager import CameraManager
from ..config import config_file, load_config, save_config
from ..patterns import generate_pattern
from ..python_engine import run_python_trap_script
from ..slm_dll_output import SlmDllOutput
from .settings_dialog import SettingsDialog


class MainWindow(QMainWindow):
    def __init__(self, base_dir: Path):
        super().__init__()
        self.base_dir = base_dir
        self.cfg_path = config_file(base_dir)
        self.config = load_config(self.cfg_path)

        self.current_points = []
        self.python_frames = []
        self.current_mask = None
        self.correction_mask = None
        self.is_dark_mode = True

        self.camera_manager = CameraManager()
        self.camera_timer = QTimer(self)
        self.camera_timer.setInterval(33)
        self.camera_timer.timeout.connect(self._on_camera_timer)
        self.camera_frame_rgb = None
        self.camera_source_size = None
        self.camera_draw_rect = QRect()
        self.selected_monitor_number = int(self.config.get("slm_selected_monitor", 1))
        self.slm_output_mode = str(self.config.get("slm_output_mode", "direct")).lower().strip()
        if self.slm_output_mode not in ("direct", "dll"):
            self.slm_output_mode = "direct"
        self.slm_window_id = int(self.config.get("slm_window_id", 0))
        self.direct_output_window = None
        self.direct_output_label = None
        self.slm_dll = SlmDllOutput(
            [
                self.base_dir / "Image_Control.dll",
                self.base_dir.parent / "src" / "hardware" / "Image_Control.dll",
            ]
        )
        self.animation_frames = []
        self.animation_masks = []
        self.animation_frame_index = 0
        self.animation_timer = QTimer(self)
        self.animation_timer.timeout.connect(self._on_animation_timer)
        self.phase_media_frames = []
        self.phase_media_index = 0
        self.phase_media_timer = QTimer(self)
        self.phase_media_timer.timeout.connect(self._on_phase_media_timer)
        self.phase_media_dialog = None
        self.phase_media_dialog_preview = None
        self.phase_media_dialog_slider = None
        self.phase_media_dialog_info = None
        self.camera_backend = int(self.config.get("camera_backend", 0))
        self.udp_bind_ip = str(self.config.get("udp_bind_ip", "0.0.0.0"))
        self.udp_port = int(self.config.get("udp_port", 9000))
        self.grid_enlarged = False
        self.selected_point_index = -1
        self.dragging_point = False
        self.python_timer = QTimer(self)
        self.python_timer.timeout.connect(self._on_python_timer)
        self.python_frame_index = 0
        self.python_playing = False
        self.camera_preview_window = None
        self.camera_preview_label = None
        self.camera_preview_monitor_number = int(self.config.get("camera_preview_monitor", 1))
        self.camera_preview_fps_label = None
        self.camera_preview_pixel_label = None
        self.camera_preview_save_btn = None
        self.camera_preview_record_btn = None
        self.camera_preview_record_time_label = None
        self.last_fps_timestamp_ms = 0.0
        self.last_fps_frame_count = 0

        self.setWindowTitle("Holographic Optical Tweezer Control")
        self.setMinimumSize(1100, 700)
        self.resize(1450, 920)

        self._build_menu()
        self._build_ui()
        self._refresh_camera_preview_monitor_options()
        self._refresh_camera_list()
        self._apply_theme(self.is_dark_mode)
        self._refresh_all_views()
        self.statusBar().showMessage("SLM: Connected | Algorithm: GS")

    def closeEvent(self, event):  # type: ignore[override]
        self._save_config()
        self.python_timer.stop()
        self.animation_timer.stop()
        self.phase_media_timer.stop()
        self._stop_camera_feed()
        self._clear_slm_output()
        if self.camera_preview_window is not None:
            self.camera_preview_window.hide()
        if self.phase_media_dialog is not None:
            self.phase_media_dialog.hide()
        super().closeEvent(event)

    def eventFilter(self, watched, event):  # type: ignore[override]
        if (watched == self.camera_view or watched == self.camera_preview_label) and event.type() == QEvent.Type.MouseMove:
            self._update_camera_pixel_readout(event.position().toPoint(), watched)
        elif (watched == self.camera_view or watched == self.camera_preview_label) and event.type() == QEvent.Type.Leave:
            self.camera_pixel_label.setText("Pixel: --, -- | RGB: --, --, --")
            if self.camera_preview_pixel_label is not None:
                self.camera_preview_pixel_label.setText("Camera: --, -- | I: --")
        elif watched == self.target_view and event.type() == QEvent.Type.MouseMove:
            self._update_grid_hover_and_drag(event.position().toPoint())
        elif watched == self.target_view and event.type() == QEvent.Type.MouseButtonPress:
            self._on_target_mouse_press(event.position().toPoint(), event.button())
        elif watched == self.target_view and event.type() == QEvent.Type.MouseButtonRelease:
            self._on_target_mouse_release(event.position().toPoint(), event.button())
        elif watched == self.target_view and event.type() == QEvent.Type.Leave:
            self.grid_hover_label.setText("Grid: --, --")
        return super().eventFilter(watched, event)

    def _build_menu(self):
        file_menu = self.menuBar().addMenu("&File")

        settings_action = QAction("Hardware Settings...", self)
        settings_action.triggered.connect(self._open_settings_dialog)
        file_menu.addAction(settings_action)

        corr_action = QAction("Load SLM Correction Mask...", self)
        corr_action.triggered.connect(self._on_load_correction)
        file_menu.addAction(corr_action)

        clear_corr_action = QAction("Clear SLM Correction", self)
        clear_corr_action.triggered.connect(self._on_clear_correction)
        file_menu.addAction(clear_corr_action)

        source_action = QAction("Source Intensity...", self)
        source_action.triggered.connect(lambda: self._show_not_implemented("Source Intensity editor"))
        file_menu.addAction(source_action)
        file_menu.addSeparator()
        file_menu.addAction("Exit", self.close)

        view_menu = self.menuBar().addMenu("&View")
        theme_action = QAction("Toggle Light/Dark Theme", self)
        theme_action.triggered.connect(self._toggle_theme)
        view_menu.addAction(theme_action)

        tools_menu = self.menuBar().addMenu("&Tools")
        holo_action = QAction("Create Hologram...", self)
        holo_action.triggered.connect(lambda: self._show_not_implemented("Hologram Generator dialog"))
        tools_menu.addAction(holo_action)

        bench_action = QAction("Compute Benchmark...", self)
        bench_action.triggered.connect(lambda: self._show_not_implemented("Compute Benchmark dialog"))
        tools_menu.addAction(bench_action)

        refresh_cams_action = QAction("Refresh Camera List", self)
        refresh_cams_action.triggered.connect(self._refresh_camera_list)
        tools_menu.addAction(refresh_cams_action)

        self.monitor_menu = tools_menu.addMenu("Select Monitor")
        self._refresh_monitor_menu()

        help_menu = self.menuBar().addMenu("&Help")
        about_action = QAction("About", self)
        about_action.triggered.connect(self._show_about)
        help_menu.addAction(about_action)

    def _build_ui(self):
        central = QWidget(self)
        self.setCentralWidget(central)
        self.main_layout = QGridLayout(central)
        self.main_layout.setSpacing(10)

        self._build_monitors()
        self._build_tools_row()
        self._build_controls()

        self.main_layout.setColumnStretch(0, 1)
        self.main_layout.setColumnStretch(1, 1)
        self.main_layout.setColumnStretch(2, 1)
        self.main_layout.setRowStretch(0, 0)
        self.main_layout.setRowStretch(1, 3)
        self.main_layout.setRowStretch(2, 0)
        self.main_layout.setRowStretch(3, 2)

    def _build_monitors(self):
        t1_wrap = QWidget()
        t1_row = QHBoxLayout(t1_wrap)
        t1_row.setContentsMargins(0, 0, 0, 0)
        t1 = QLabel("Target Pattern (Interactive Grid)")
        t1.setAlignment(Qt.AlignmentFlag.AlignCenter)
        t1_row.addWidget(t1, 1)
        self.grid_maxmin_btn = QPushButton("Enlarge")
        self.grid_maxmin_btn.setFixedWidth(72)
        self.grid_maxmin_btn.clicked.connect(self._toggle_grid_enlarged)
        t1_row.addWidget(self.grid_maxmin_btn, 0, Qt.AlignmentFlag.AlignRight)
        t2 = QLabel("Phase Mask")
        t2.setAlignment(Qt.AlignmentFlag.AlignCenter)
        t3 = QLabel("Live Camera Feed")
        t3.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.monitor_title_target = t1_wrap
        self.monitor_title_phase = t2
        self.monitor_title_camera = t3
        self.main_layout.addWidget(t1_wrap, 0, 0)
        self.main_layout.addWidget(t2, 0, 1)
        self.main_layout.addWidget(t3, 0, 2)

        self.target_view = QLabel("No target points")
        self.target_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.target_view.setMinimumHeight(320)
        self.target_view.setMouseTracking(True)
        self.target_view.installEventFilter(self)
        self.target_view.setStyleSheet("border: 1px solid #666; background: #0d0d0d;")

        self.phase_mask_view = QLabel("No mask")
        self.phase_mask_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.phase_mask_view.setMinimumHeight(320)
        self.phase_mask_view.setStyleSheet("border: 1px solid #666; background: #0d0d0d;")

        self.camera_view = QLabel("Camera feed offline")
        self.camera_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.camera_view.setMinimumHeight(320)
        self.camera_view.setMouseTracking(True)
        self.camera_view.installEventFilter(self)
        self.camera_view.setStyleSheet("border: 1px solid #666; background: #0d0d0d;")

        self.main_layout.addWidget(self.target_view, 1, 0)
        self.main_layout.addWidget(self.phase_mask_view, 1, 1)
        self.main_layout.addWidget(self.camera_view, 1, 2)

    def _build_tools_row(self):
        self.tools_row = QWidget()
        row = QHBoxLayout(self.tools_row)
        row.setContentsMargins(0, 0, 0, 0)
        self.save_mask_btn = QPushButton("Save Mask")
        self.save_mask_btn.clicked.connect(self._on_save_mask)
        row.addWidget(self.save_mask_btn)
        self.preview_corr_cb = QCheckBox("Preview Correction")
        self.preview_corr_cb.setChecked(True)
        self.preview_corr_cb.toggled.connect(lambda _checked: self._refresh_mask_view())
        row.addWidget(self.preview_corr_cb)
        self.overlay_target_cb = QCheckBox("Overlay target on camera")
        self.overlay_target_cb.setChecked(False)
        self.overlay_target_cb.toggled.connect(lambda _checked: self._refresh_camera_view())
        row.addWidget(self.overlay_target_cb)
        self.camera_preview_monitor_combo = QComboBox()
        self.camera_preview_monitor_combo.setMinimumWidth(180)
        self.camera_preview_monitor_combo.currentIndexChanged.connect(self._on_camera_preview_monitor_changed)
        row.addWidget(self.camera_preview_monitor_combo)
        self.camera_preview_toggle_btn = QPushButton("Show On Monitor")
        self.camera_preview_toggle_btn.setCheckable(True)
        self.camera_preview_toggle_btn.toggled.connect(self._on_camera_preview_toggled)
        row.addWidget(self.camera_preview_toggle_btn)
        self.grid_hover_label = QLabel("Grid: --, --")
        row.addWidget(self.grid_hover_label)
        row.addStretch()
        self.main_layout.addWidget(self.tools_row, 2, 0, 1, 3)

    def _build_controls(self):
        self.controls_row = QWidget()
        controls = QHBoxLayout(self.controls_row)
        controls.setContentsMargins(0, 0, 0, 0)
        controls.setSpacing(10)

        left = self._build_left_column()
        mid = self._build_mid_column()
        right = self._build_right_column()

        controls.addWidget(left, 1)
        controls.addWidget(mid, 1)
        controls.addWidget(right, 1)
        self.main_layout.addWidget(self.controls_row, 3, 0, 1, 3)

    def _toggle_grid_enlarged(self):
        self.grid_enlarged = not self.grid_enlarged
        if self.grid_enlarged:
            self.monitor_title_phase.setVisible(False)
            self.monitor_title_camera.setVisible(False)
            self.phase_mask_view.setVisible(False)
            self.camera_view.setVisible(False)
            self.tools_row.setVisible(False)
            self.controls_row.setVisible(False)
            self.grid_maxmin_btn.setText("Restore")
        else:
            self.monitor_title_phase.setVisible(True)
            self.monitor_title_camera.setVisible(True)
            self.phase_mask_view.setVisible(True)
            self.camera_view.setVisible(True)
            self.tools_row.setVisible(True)
            self.controls_row.setVisible(True)
            self.grid_maxmin_btn.setText("Enlarge")

    def _build_left_column(self):
        w = QWidget()
        layout = QVBoxLayout(w)
        self.target_tabs = QTabWidget()
        layout.addWidget(self.target_tabs)

        self._build_manual_tab()
        self._build_pattern_tab()
        self._build_image_tab()
        self._build_camera_tab()
        self._build_animation_tab()
        self._build_python_tab()
        return w

    def _build_manual_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        btns = QHBoxLayout()
        add_btn = QPushButton("Add Points")
        add_btn.clicked.connect(self._manual_add_point)
        clear_btn = QPushButton("Clear All")
        clear_btn.clicked.connect(self._manual_clear_points)
        btns.addWidget(add_btn)
        btns.addWidget(clear_btn)
        layout.addLayout(btns)

        self.manual_table = QTableWidget(0, 3)
        self.manual_table.setHorizontalHeaderLabels(["No", "X", "Y"])
        layout.addWidget(self.manual_table)

        apply_btn = QPushButton("Apply Manual Points")
        apply_btn.clicked.connect(self._manual_apply_points)
        layout.addWidget(apply_btn)
        self.target_tabs.addTab(tab, "Manual")

    def _build_pattern_tab(self):
        tab = QWidget()
        form = QFormLayout(tab)
        self.pattern_combo = QComboBox()
        self.pattern_combo.addItems(
            ["circle", "triangle", "square", "rectangle", "hexagon", "two_spots", "star", "planet_moon", "grid"]
        )
        self.p_count = QSpinBox()
        self.p_count.setRange(1, 400)
        self.p_count.setValue(12)
        self.p_radius = QDoubleSpinBox()
        self.p_radius.setRange(1, 3000)
        self.p_radius.setValue(120)
        self.p_size = QDoubleSpinBox()
        self.p_size.setRange(1, 3000)
        self.p_size.setValue(140)
        self.p_width = QDoubleSpinBox()
        self.p_width.setRange(1, 3000)
        self.p_width.setValue(220)
        self.p_height = QDoubleSpinBox()
        self.p_height.setRange(1, 3000)
        self.p_height.setValue(120)
        self.p_rot = QDoubleSpinBox()
        self.p_rot.setRange(-360, 360)
        self.p_x = QDoubleSpinBox()
        self.p_x.setRange(-5000, 5000)
        self.p_y = QDoubleSpinBox()
        self.p_y.setRange(-5000, 5000)
        self.p_star = QSpinBox()
        self.p_star.setRange(3, 20)
        self.p_star.setValue(5)
        self.p_inner = QDoubleSpinBox()
        self.p_inner.setRange(1, 3000)
        self.p_inner.setValue(60)
        self.p_moon = QDoubleSpinBox()
        self.p_moon.setRange(1, 3000)
        self.p_moon.setValue(30)
        self.p_dist = QDoubleSpinBox()
        self.p_dist.setRange(1, 3000)
        self.p_dist.setValue(150)
        self.p_rows = QSpinBox()
        self.p_rows.setRange(1, 50)
        self.p_rows.setValue(4)
        self.p_cols = QSpinBox()
        self.p_cols.setRange(1, 50)
        self.p_cols.setValue(4)
        self.p_rs = QDoubleSpinBox()
        self.p_rs.setRange(1, 1000)
        self.p_rs.setValue(40)
        self.p_cs = QDoubleSpinBox()
        self.p_cs.setRange(1, 1000)
        self.p_cs.setValue(40)

        form.addRow("Preset", self.pattern_combo)
        form.addRow("No. of points", self.p_count)
        form.addRow("Radius", self.p_radius)
        form.addRow("Size", self.p_size)
        form.addRow("Width", self.p_width)
        form.addRow("Height", self.p_height)
        form.addRow("Rotation", self.p_rot)
        form.addRow("X Shift", self.p_x)
        form.addRow("Y Shift", self.p_y)
        form.addRow("Star points", self.p_star)
        form.addRow("Inner radius", self.p_inner)
        form.addRow("Moon radius", self.p_moon)
        form.addRow("Distance", self.p_dist)
        form.addRow("Grid rows", self.p_rows)
        form.addRow("Grid cols", self.p_cols)
        form.addRow("Row spacing", self.p_rs)
        form.addRow("Col spacing", self.p_cs)
        gen_btn = QPushButton("Generate Pattern")
        gen_btn.clicked.connect(self._pattern_generate_points)
        form.addRow(gen_btn)
        self.target_tabs.addTab(tab, "Pattern")

    def _build_image_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        load_btn = QPushButton("Load Image")
        load_btn.clicked.connect(self._load_target_image)
        clear_btn = QPushButton("Clear Image")
        clear_btn.clicked.connect(self._clear_target_image)
        row = QHBoxLayout()
        row.addWidget(load_btn)
        row.addWidget(clear_btn)
        row.addStretch()
        layout.addLayout(row)
        self.image_info = QLabel("No image loaded. Will convert bright pixels into target points.")
        self.image_info.setWordWrap(True)
        layout.addWidget(self.image_info)
        layout.addStretch()
        self.target_tabs.addTab(tab, "Image")

    def _build_camera_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        info = QLabel("Live camera points table mirrors the shared target list.")
        info.setWordWrap(True)
        layout.addWidget(info)
        btns = QHBoxLayout()
        add_btn = QPushButton("Add Points")
        add_btn.clicked.connect(self._manual_add_point)
        clear_btn = QPushButton("Clear All")
        clear_btn.clicked.connect(self._manual_clear_points)
        btns.addWidget(add_btn)
        btns.addWidget(clear_btn)
        layout.addLayout(btns)
        self.camera_table = QTableWidget(0, 3)
        self.camera_table.setHorizontalHeaderLabels(["No", "X", "Y"])
        layout.addWidget(self.camera_table)
        self.target_tabs.addTab(tab, "Camera")

    def _build_animation_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        info = QLabel("Generate and play point animation sequences.")
        info.setWordWrap(True)
        layout.addWidget(info)
        settings_group = QGroupBox("Animation Settings")
        form = QFormLayout(settings_group)
        self.anim_preset = QComboBox()
        self.anim_preset.addItems(["Circle", "Triangle"])
        self.anim_preset.currentIndexChanged.connect(self._on_animation_preset_changed)
        self.anim_fps = QSpinBox()
        self.anim_fps.setRange(1, 240)
        self.anim_fps.setValue(30)
        self.anim_frames = QSpinBox()
        self.anim_frames.setRange(1, 1000)
        self.anim_frames.setValue(60)
        self.anim_particles = QSpinBox()
        self.anim_particles.setRange(1, 2000)
        self.anim_particles.setValue(24)
        self.anim_realtime = QCheckBox("Realtime")
        self.anim_realtime.setChecked(True)
        self.anim_send_slm = QCheckBox("Send each frame to SLM")
        self.anim_send_slm.setChecked(False)
        form.addRow("Preset:", self.anim_preset)
        form.addRow("Frame rate (FPS):", self.anim_fps)
        form.addRow("No. of frames:", self.anim_frames)
        form.addRow("No. of particles:", self.anim_particles)
        form.addRow("Mode:", self.anim_realtime)
        form.addRow("", self.anim_send_slm)

        self.anim_params_stack = QStackedWidget()
        circle_page = QWidget()
        circle_form = QFormLayout(circle_page)
        self.anim_circle_radius_from = QDoubleSpinBox()
        self.anim_circle_radius_from.setRange(1.0, 3000.0)
        self.anim_circle_radius_from.setValue(80.0)
        self.anim_circle_radius_to = QDoubleSpinBox()
        self.anim_circle_radius_to.setRange(1.0, 3000.0)
        self.anim_circle_radius_to.setValue(180.0)
        circle_form.addRow("Radius from:", self.anim_circle_radius_from)
        circle_form.addRow("Radius to:", self.anim_circle_radius_to)

        triangle_page = QWidget()
        triangle_form = QFormLayout(triangle_page)
        self.anim_triangle_scale_from = QDoubleSpinBox()
        self.anim_triangle_scale_from.setRange(1.0, 3000.0)
        self.anim_triangle_scale_from.setValue(80.0)
        self.anim_triangle_scale_to = QDoubleSpinBox()
        self.anim_triangle_scale_to.setRange(1.0, 3000.0)
        self.anim_triangle_scale_to.setValue(180.0)
        self.anim_triangle_rot_from = QDoubleSpinBox()
        self.anim_triangle_rot_from.setRange(-3600.0, 3600.0)
        self.anim_triangle_rot_from.setValue(0.0)
        self.anim_triangle_rot_to = QDoubleSpinBox()
        self.anim_triangle_rot_to.setRange(-3600.0, 3600.0)
        self.anim_triangle_rot_to.setValue(360.0)
        triangle_form.addRow("Scale from:", self.anim_triangle_scale_from)
        triangle_form.addRow("Scale to:", self.anim_triangle_scale_to)
        triangle_form.addRow("Rotation from:", self.anim_triangle_rot_from)
        triangle_form.addRow("Rotation to:", self.anim_triangle_rot_to)

        self.anim_params_stack.addWidget(circle_page)
        self.anim_params_stack.addWidget(triangle_page)
        form.addRow("Preset params:", self.anim_params_stack)
        layout.addWidget(settings_group)

        preview_row = QHBoxLayout()
        self.anim_preview_label = QLabel("No frame")
        self.anim_preview_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.anim_preview_label.setMinimumHeight(90)
        self.anim_preview_label.setStyleSheet("border: 1px solid #666; background: #0d0d0d;")
        self.anim_camera_preview_label = QLabel("No frame")
        self.anim_camera_preview_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.anim_camera_preview_label.setMinimumHeight(90)
        self.anim_camera_preview_label.setStyleSheet("border: 1px solid #666; background: #0d0d0d;")
        preview_row.addWidget(self.anim_preview_label, 1)
        preview_row.addWidget(self.anim_camera_preview_label, 1)
        layout.addLayout(preview_row)

        buttons = QHBoxLayout()
        self.anim_generate_btn = QPushButton("Generate Sequence")
        self.anim_generate_btn.clicked.connect(self._generate_animation_sequence)
        self.anim_play_btn = QPushButton("Play/Send")
        self.anim_play_btn.clicked.connect(self._play_animation_sequence)
        self.anim_stop_btn = QPushButton("Stop")
        self.anim_stop_btn.clicked.connect(self._stop_animation_sequence)
        self.anim_reset_btn = QPushButton("Reset")
        self.anim_reset_btn.clicked.connect(self._reset_animation_sequence)
        buttons.addWidget(self.anim_generate_btn)
        buttons.addWidget(self.anim_play_btn)
        buttons.addWidget(self.anim_stop_btn)
        buttons.addWidget(self.anim_reset_btn)
        layout.addLayout(buttons)
        self.anim_stop_btn.setEnabled(False)
        self._on_animation_preset_changed(self.anim_preset.currentIndex())
        self.target_tabs.addTab(tab, "Animation")

    def _build_python_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        self.python_editor = QTextEdit()
        self.python_editor.setPlainText(
            "def build_pattern(width, height):\n"
            "    return hot.pattern.circle(point_count=12, radius=min(width, height)*0.15)\n"
        )
        layout.addWidget(self.python_editor, 1)
        settings_row = QHBoxLayout()
        self.python_frames_spin = QSpinBox()
        self.python_frames_spin.setRange(1, 1000)
        self.python_frames_spin.setValue(120)
        self.python_fps_spin = QSpinBox()
        self.python_fps_spin.setRange(1, 240)
        self.python_fps_spin.setValue(30)
        self.python_realtime_cb = QCheckBox("Realtime")
        self.python_realtime_cb.setChecked(True)
        self.python_trap_selector = QComboBox()
        self.python_trap_selector.addItem("None", -1)
        settings_row.addWidget(QLabel("No. of frames"))
        settings_row.addWidget(self.python_frames_spin)
        settings_row.addWidget(QLabel("FPS"))
        settings_row.addWidget(self.python_fps_spin)
        settings_row.addWidget(self.python_realtime_cb)
        settings_row.addWidget(QLabel("Trap"))
        settings_row.addWidget(self.python_trap_selector)
        settings_row.addStretch()
        layout.addLayout(settings_row)

        row = QHBoxLayout()
        run_btn = QPushButton("Run Code")
        run_btn.clicked.connect(self._run_python_code)
        row.addWidget(run_btn)
        self.python_send_btn = QPushButton("Send")
        self.python_send_btn.clicked.connect(self._play_python_sequence)
        row.addWidget(self.python_send_btn)
        self.python_stop_btn = QPushButton("Stop")
        self.python_stop_btn.clicked.connect(self._stop_python_sequence)
        self.python_stop_btn.setEnabled(False)
        row.addWidget(self.python_stop_btn)
        self.python_reset_btn = QPushButton("Reset")
        self.python_reset_btn.clicked.connect(self._reset_python_sequence)
        row.addWidget(self.python_reset_btn)
        save_btn = QPushButton("Save .py")
        save_btn.clicked.connect(self._save_python_script)
        row.addWidget(save_btn)
        load_btn = QPushButton("Load .py")
        load_btn.clicked.connect(self._load_python_script)
        row.addWidget(load_btn)
        layout.addLayout(row)
        self.python_info = QLabel("No script run yet.")
        layout.addWidget(self.python_info)
        self.target_tabs.addTab(tab, "Python")

    def _build_mid_column(self):
        w = QWidget()
        layout = QVBoxLayout(w)
        algo_group = QGroupBox("Algorithm Settings")
        form = QFormLayout(algo_group)
        self.algorithm_combo = QComboBox()
        self.algorithm_combo.addItems(["Gerchberg-Saxton", "Weighted GS", "Random Mask Encoding (Paper)"])
        self.iter_spin = QSpinBox()
        self.iter_spin.setRange(1, 1000)
        self.iter_spin.setValue(int(self.config["iterations"]))
        self.relax_spin = QDoubleSpinBox()
        self.relax_spin.setRange(0.0, 1.0)
        self.relax_spin.setSingleStep(0.05)
        self.relax_spin.setDecimals(2)
        self.relax_spin.setValue(float(self.config["wgs_relaxation"]))
        gen_btn = QPushButton("Generate GS Mask")
        gen_btn.clicked.connect(self._generate_mask_clicked)
        form.addRow("Algorithm:", self.algorithm_combo)
        form.addRow("Iterations:", self.iter_spin)
        form.addRow("Relaxation:", self.relax_spin)
        form.addRow(gen_btn)
        layout.addWidget(algo_group)
        layout.addStretch()
        return w

    def _build_right_column(self):
        w = QWidget()
        layout = QVBoxLayout(w)

        cam_group = QGroupBox("Camera Control")
        cam_form = QFormLayout(cam_group)
        self.cam_select = QComboBox()
        cam_form.addRow("Camera:", self.cam_select)
        self.camera_source_label = QLabel("")
        self.camera_source_label.setWordWrap(True)
        cam_form.addRow("Source:", self.camera_source_label)

        refresh_btn = QPushButton("Refresh List")
        refresh_btn.clicked.connect(self._refresh_camera_list)
        cam_form.addRow("Devices:", refresh_btn)

        cap_row = QHBoxLayout()
        self.capture_image_btn = QPushButton("Save Image")
        self.capture_image_btn.clicked.connect(self._save_camera_snapshot)
        self.record_video_btn = QPushButton("Record Video")
        self.record_video_btn.setCheckable(True)
        self.record_video_btn.toggled.connect(self._toggle_recording)
        cap_row.addWidget(self.capture_image_btn)
        cap_row.addWidget(self.record_video_btn)
        cam_form.addRow("Capture:", cap_row)

        run_row = QHBoxLayout()
        self.cam_start_btn = QPushButton("Start Feed")
        self.cam_start_btn.clicked.connect(self._start_camera_feed)
        self.cam_stop_btn = QPushButton("Stop Feed")
        self.cam_stop_btn.clicked.connect(self._stop_camera_feed)
        run_row.addWidget(self.cam_start_btn)
        run_row.addWidget(self.cam_stop_btn)
        cam_form.addRow(run_row)

        self.camera_pixel_label = QLabel("Pixel: --, -- | RGB: --, --, --")
        cam_form.addRow("Readout:", self.camera_pixel_label)

        slm_group = QGroupBox("SLM Control")
        slm_layout = QVBoxLayout(slm_group)
        slm_status = QLabel("SLM: Connected")
        slm_status.setStyleSheet("color: #4CAF50; font-weight: bold;")
        slm_layout.addWidget(slm_status)
        self.slm_output_mode_combo = QComboBox()
        self.slm_output_mode_combo.addItem("Direct Screen (Fullscreen)", "direct")
        self.slm_output_mode_combo.addItem("Image_Control.dll", "dll")
        mode_idx = 0 if self.slm_output_mode == "direct" else 1
        self.slm_output_mode_combo.setCurrentIndex(mode_idx)
        self.slm_output_mode_combo.currentIndexChanged.connect(self._on_slm_output_mode_changed)
        slm_layout.addWidget(self.slm_output_mode_combo)
        self.slm_output_mode_label = QLabel("")
        self.slm_output_mode_label.setStyleSheet("color: #9CA3AF;")
        slm_layout.addWidget(self.slm_output_mode_label)
        load_phase = QPushButton("Load Phase Mask")
        load_phase.clicked.connect(self._load_phase_mask_source)
        send_slm = QPushButton("Send to SLM")
        send_slm.clicked.connect(self._send_to_slm)
        clear_slm = QPushButton("Clear SLM")
        clear_slm.clicked.connect(self._clear_slm)
        slm_layout.addWidget(load_phase)
        slm_layout.addWidget(send_slm)
        slm_layout.addWidget(clear_slm)
        media_row = QHBoxLayout()
        self.phase_media_play_btn = QPushButton("Play Media")
        self.phase_media_play_btn.clicked.connect(self._toggle_phase_media_play)
        self.phase_media_stop_btn = QPushButton("Stop Media")
        self.phase_media_stop_btn.clicked.connect(self._stop_phase_media)
        media_row.addWidget(self.phase_media_play_btn)
        media_row.addWidget(self.phase_media_stop_btn)
        slm_layout.addLayout(media_row)
        self.phase_media_fps = QSpinBox()
        self.phase_media_fps.setRange(1, 240)
        self.phase_media_fps.setValue(30)
        slm_layout.addWidget(QLabel("Media FPS"))
        slm_layout.addWidget(self.phase_media_fps)
        self.phase_media_send_slm = QCheckBox("Send media frames to SLM")
        self.phase_media_send_slm.setChecked(True)
        slm_layout.addWidget(self.phase_media_send_slm)
        self.phase_media_info = QLabel("Media: none")
        self.phase_media_info.setWordWrap(True)
        slm_layout.addWidget(self.phase_media_info)
        self._update_slm_output_label()
        self._update_camera_source_label()

        layout.addWidget(cam_group)
        layout.addWidget(slm_group)
        layout.addStretch()
        return w

    def _algorithm_name(self):
        idx = self.algorithm_combo.currentIndex()
        if idx == 1:
            return "WGS"
        if idx == 2:
            return "RME"
        return "GS"

    def _settings_dict(self):
        return {
            "slm_width": int(self.config.get("slm_width", 1920)),
            "slm_height": int(self.config.get("slm_height", 1080)),
            "slm_pixel_size_um": float(self.config.get("slm_pixel_size_um", 8.0)),
            "cam_width": int(self.config.get("cam_width", 1920)),
            "cam_height": int(self.config.get("cam_height", 1080)),
            "cam_pixel_size_um": float(self.config.get("cam_pixel_size_um", 5.0)),
            "camera_imaging_magnification": float(self.config.get("camera_imaging_magnification", 1.0)),
            "wavelength_nm": float(self.config.get("wavelength_nm", 1064.0)),
            "focal_length_mm": float(self.config.get("focal_length_mm", 100.0)),
            "iterations": int(self.iter_spin.value()),
            "wgs_relaxation": float(self.relax_spin.value()),
        }

    def _save_config(self):
        self.config["iterations"] = int(self.iter_spin.value())
        self.config["wgs_relaxation"] = float(self.relax_spin.value())
        self.config["slm_selected_monitor"] = int(self.selected_monitor_number)
        self.config["slm_output_mode"] = str(self.slm_output_mode)
        self.config["slm_window_id"] = int(self.slm_window_id)
        self.config["camera_preview_monitor"] = int(self.camera_preview_monitor_number)
        self.config["camera_backend"] = int(self.camera_backend)
        self.config["udp_bind_ip"] = str(self.udp_bind_ip)
        self.config["udp_port"] = int(self.udp_port)
        save_config(self.cfg_path, self.config)

    def _open_settings_dialog(self):
        dlg = SettingsDialog(self, dict(self.config), self.selected_monitor_number)
        if not dlg.exec():
            return
        out = dlg.result_payload()
        self.config.update(out["config"])
        self.selected_monitor_number = int(out["selected_monitor"])
        self.camera_backend = int(self.config.get("camera_backend", self.camera_backend))
        self.udp_bind_ip = str(self.config.get("udp_bind_ip", self.udp_bind_ip))
        self.udp_port = int(self.config.get("udp_port", self.udp_port))
        self._update_camera_source_label()
        self._stop_camera_feed()
        self._refresh_camera_list()
        self.slm_output_mode = str(self.config.get("slm_output_mode", self.slm_output_mode)).lower().strip()
        if self.slm_output_mode not in ("direct", "dll"):
            self.slm_output_mode = "direct"
        self.slm_window_id = int(self.config.get("slm_window_id", self.slm_window_id))
        if self.slm_output_mode_combo is not None:
            self.slm_output_mode_combo.blockSignals(True)
            self.slm_output_mode_combo.setCurrentIndex(0 if self.slm_output_mode == "direct" else 1)
            self.slm_output_mode_combo.blockSignals(False)
        self._update_slm_output_label()
        self._refresh_monitor_menu()
        self._refresh_target_view()
        self._refresh_camera_view()
        self.statusBar().showMessage("Hardware settings applied.", 3000)

    def _update_camera_source_label(self):
        if self.camera_backend == 2:
            self.camera_source_label.setText(f"UDP: {self.udp_bind_ip}:{self.udp_port}")
        elif self.camera_backend == 1:
            self.camera_source_label.setText("DirectShow device")
        else:
            self.camera_source_label.setText("Default camera device")

    def _on_slm_output_mode_changed(self, _index):
        mode = self.slm_output_mode_combo.currentData()
        if str(mode) != self.slm_output_mode:
            self._clear_slm_output()
        self.slm_output_mode = str(mode)
        self.config["slm_output_mode"] = self.slm_output_mode
        self._update_slm_output_label()

    def _update_slm_output_label(self):
        if self.slm_output_mode == "dll":
            dll_ok = self.slm_dll.load()
            if dll_ok:
                self.slm_output_mode_label.setText(f"Output: DLL loaded ({Path(self.slm_dll.loaded_path).name})")
            else:
                self.slm_output_mode_label.setText(f"Output: DLL unavailable ({self.slm_dll.last_error})")
        else:
            self.slm_output_mode_label.setText("Output: Direct Screen (Fullscreen)")

    def _on_animation_preset_changed(self, index):
        self.anim_params_stack.setCurrentIndex(0 if index == 0 else 1)

    def _generate_animation_sequence(self):
        frame_count = max(1, int(self.anim_frames.value()))
        points_count = max(1, int(self.anim_particles.value()))
        frames = []
        masks = []
        for i in range(frame_count):
            t = 0.0 if frame_count <= 1 else i / float(frame_count - 1)
            if self.anim_preset.currentIndex() == 0:
                radius = self.anim_circle_radius_from.value() + (
                    self.anim_circle_radius_to.value() - self.anim_circle_radius_from.value()
                ) * t
                req = {
                    "preset": "circle",
                    "point_count": points_count,
                    "radius": radius,
                    "rotation_deg": 0.0,
                    "x_shift": 0.0,
                    "y_shift": 0.0,
                }
            else:
                scale = self.anim_triangle_scale_from.value() + (
                    self.anim_triangle_scale_to.value() - self.anim_triangle_scale_from.value()
                ) * t
                rot = self.anim_triangle_rot_from.value() + (
                    self.anim_triangle_rot_to.value() - self.anim_triangle_rot_from.value()
                ) * t
                req = {
                    "preset": "triangle",
                    "point_count": points_count,
                    "size": scale,
                    "rotation_deg": rot,
                    "x_shift": 0.0,
                    "y_shift": 0.0,
                }
            frame_points = generate_pattern(req)
            frames.append(frame_points)
            if not self.anim_realtime.isChecked():
                try:
                    mask, _ = generate_phase_mask(frame_points, self._settings_dict(), self._algorithm_name())
                    masks.append(mask)
                except Exception:
                    masks.append(None)
        self.animation_frames = frames
        self.animation_masks = masks
        self.animation_frame_index = 0
        if self.animation_frames:
            self.current_points = list(self.animation_frames[0])
            self._sync_manual_table(self.current_points)
            self._sync_camera_table(self.current_points)
            self._refresh_target_view()
            self._update_animation_preview_labels(self.current_points)
            self.anim_preview_label.setToolTip(
                f"Sequence ready: {len(self.animation_frames)} frames, {len(self.current_points)} points/frame"
            )
        else:
            self.anim_preview_label.setText("No animation frames generated.")
        self.statusBar().showMessage("Animation sequence generated.", 3000)

    def _play_animation_sequence(self):
        if not self.animation_frames:
            QMessageBox.information(self, "Animation", "Generate Sequence first.")
            return
        interval_ms = max(1, int(round(1000.0 / max(1, self.anim_fps.value()))))
        self.animation_timer.start(interval_ms)
        self.anim_stop_btn.setEnabled(True)
        self.statusBar().showMessage("Animation playback started.", 2000)

    def _stop_animation_sequence(self):
        self.animation_timer.stop()
        self.anim_stop_btn.setEnabled(False)
        self.statusBar().showMessage("Animation playback stopped.", 2000)

    def _reset_animation_sequence(self):
        self._stop_animation_sequence()
        self.animation_frame_index = 0
        if self.animation_frames:
            self.current_points = list(self.animation_frames[0])
            self._sync_manual_table(self.current_points)
            self._sync_camera_table(self.current_points)
            self._refresh_target_view()
            self._update_animation_preview_labels(self.current_points)
        else:
            self.anim_preview_label.setText("No animation sequence")

    def _on_animation_timer(self):
        if not self.animation_frames:
            self._stop_animation_sequence()
            return
        idx = self.animation_frame_index % len(self.animation_frames)
        self.current_points = list(self.animation_frames[idx])
        self._sync_manual_table(self.current_points)
        self._sync_camera_table(self.current_points)
        self._refresh_target_view()
        self._refresh_camera_view()
        mask = None
        if self.anim_realtime.isChecked():
            try:
                mask, _ = generate_phase_mask(self.current_points, self._settings_dict(), self._algorithm_name())
            except Exception:
                mask = None
        elif self.animation_masks:
            mask = self.animation_masks[idx]
        if mask is not None:
            self.current_mask = mask
            self._refresh_mask_view()
            if self.anim_send_slm.isChecked():
                self._send_mask_to_output(mask)
        self.anim_preview_label.setText(f"Playing frame {idx + 1}/{len(self.animation_frames)}")
        self._update_animation_preview_labels(self.current_points)
        self.animation_frame_index = (idx + 1) % len(self.animation_frames)

    def _update_animation_preview_labels(self, points):
        w, h = 220, 120
        cam_w = max(1.0, float(self.config.get("cam_width", 1920)) / 2.0)
        cam_h = max(1.0, float(self.config.get("cam_height", 1080)) / 2.0)
        for label, color in ((self.anim_preview_label, (0, 220, 255)), (self.anim_camera_preview_label, (0, 190, 120))):
            canvas = np.zeros((h, w, 3), dtype=np.uint8)
            canvas[:, w // 2 : w // 2 + 1] = (40, 40, 40)
            canvas[h // 2 : h // 2 + 1, :] = (40, 40, 40)
            for x, y in points:
                px = int(round(w / 2.0 + (x / cam_w) * (w / 2.0 - 6)))
                py = int(round(h / 2.0 - (y / cam_h) * (h / 2.0 - 6)))
                if 0 <= px < w and 0 <= py < h:
                    canvas[max(0, py - 2) : min(h, py + 3), max(0, px - 2) : min(w, px + 3)] = color
            qimg = QImage(canvas.data, w, h, canvas.strides[0], QImage.Format.Format_RGB888).copy()
            label.setPixmap(QPixmap.fromImage(qimg))

    def _load_phase_mask_source(self):
        chooser = QMessageBox(self)
        chooser.setWindowTitle("Load Phase Mask")
        chooser.setText("Select phase mask source type:")
        image_btn = chooser.addButton("Image", QMessageBox.ButtonRole.ActionRole)
        video_btn = chooser.addButton("Video", QMessageBox.ButtonRole.ActionRole)
        folder_btn = chooser.addButton("Image Sequence Folder", QMessageBox.ButtonRole.ActionRole)
        chooser.addButton(QMessageBox.StandardButton.Cancel)
        chooser.exec()
        clicked = chooser.clickedButton()
        if clicked == image_btn:
            self._load_phase_mask_image()
            return
        if clicked == video_btn:
            self._load_phase_mask_video()
            return
        if clicked == folder_btn:
            self._load_phase_mask_folder()

    def _load_phase_mask_video(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select Phase Mask Video", "", "Video Files (*.mp4 *.avi *.mov *.mkv)")
        if not path:
            return
        cap = cv2.VideoCapture(path)
        if not cap.isOpened():
            QMessageBox.warning(self, "Phase Video", "Failed to open video file.")
            return
        frames = []
        while True:
            ok, frame = cap.read()
            if not ok or frame is None:
                break
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            frames.append(gray.astype(np.uint8))
            if len(frames) >= 1500:
                break
        cap.release()
        if not frames:
            QMessageBox.warning(self, "Phase Video", "No frames decoded from video.")
            return
        self.phase_media_frames = frames
        self.phase_media_index = 0
        self.current_mask = self.phase_media_frames[0]
        self._refresh_mask_view()
        self._ensure_phase_media_dialog()
        self._update_phase_media_dialog_ui()
        self.phase_media_dialog.show()
        self.phase_media_info.setText(f"Media: {Path(path).name} ({len(frames)} frames)")
        self.statusBar().showMessage(f"Loaded phase video with {len(frames)} frames.", 4000)

    def _load_phase_mask_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Select Phase Mask Image Sequence Folder")
        if not folder:
            return
        p = Path(folder)
        files = []
        for pattern in ("*.png", "*.bmp", "*.jpg", "*.jpeg", "*.tif", "*.tiff"):
            files.extend(sorted(p.glob(pattern)))
        if not files:
            QMessageBox.warning(self, "Phase Sequence", "No image files found in selected folder.")
            return
        frames = []
        for f in files:
            arr = np.asarray(Image.open(f).convert("L"), dtype=np.uint8)
            frames.append(arr)
            if len(frames) >= 1500:
                break
        self.phase_media_frames = frames
        self.phase_media_index = 0
        self.current_mask = self.phase_media_frames[0]
        self._refresh_mask_view()
        self._ensure_phase_media_dialog()
        self._update_phase_media_dialog_ui()
        self.phase_media_dialog.show()
        self.phase_media_info.setText(f"Media: {p.name} ({len(frames)} frames)")
        self.statusBar().showMessage(f"Loaded phase image sequence with {len(frames)} frames.", 4000)

    def _ensure_phase_media_dialog(self):
        if self.phase_media_dialog is not None:
            return
        dlg = QDialog(self)
        dlg.setWindowTitle("Phase Mask Media Player")
        dlg.resize(640, 520)
        layout = QVBoxLayout(dlg)
        self.phase_media_dialog_preview = QLabel("No media loaded.")
        self.phase_media_dialog_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.phase_media_dialog_preview.setMinimumSize(560, 320)
        self.phase_media_dialog_preview.setStyleSheet("border:1px solid #666; background:#0d0d0d;")
        layout.addWidget(self.phase_media_dialog_preview, 1)
        self.phase_media_dialog_slider = QSlider(Qt.Orientation.Horizontal)
        self.phase_media_dialog_slider.valueChanged.connect(self._on_phase_media_slider)
        layout.addWidget(self.phase_media_dialog_slider)
        self.phase_media_dialog_info = QLabel("Frame 0 / 0")
        layout.addWidget(self.phase_media_dialog_info)
        btn_row = QHBoxLayout()
        play_btn = QPushButton("Play/Pause")
        play_btn.clicked.connect(self._toggle_phase_media_play)
        stop_btn = QPushButton("Stop")
        stop_btn.clicked.connect(self._stop_phase_media)
        close_btn = QPushButton("Close")
        close_btn.clicked.connect(dlg.hide)
        btn_row.addWidget(play_btn)
        btn_row.addWidget(stop_btn)
        btn_row.addStretch()
        btn_row.addWidget(close_btn)
        layout.addLayout(btn_row)
        self.phase_media_dialog = dlg

    def _on_phase_media_slider(self, value):
        if not self.phase_media_frames:
            return
        idx = int(np.clip(value, 0, len(self.phase_media_frames) - 1))
        self.phase_media_index = idx
        frame = self.phase_media_frames[idx]
        self.current_mask = frame
        self._refresh_mask_view()
        if self.phase_media_send_slm.isChecked():
            self._send_mask_to_output(frame)
        self._update_phase_media_dialog_ui()

    def _update_phase_media_dialog_ui(self):
        if self.phase_media_dialog is None:
            return
        total = len(self.phase_media_frames)
        if total <= 0:
            self.phase_media_dialog_info.setText("Frame 0 / 0")
            self.phase_media_dialog_slider.setRange(0, 0)
            return
        self.phase_media_dialog_slider.blockSignals(True)
        self.phase_media_dialog_slider.setRange(0, total - 1)
        self.phase_media_dialog_slider.setValue(self.phase_media_index)
        self.phase_media_dialog_slider.blockSignals(False)
        self.phase_media_dialog_info.setText(f"Frame {self.phase_media_index + 1} / {total}")
        frame = self.phase_media_frames[self.phase_media_index]
        h, w = frame.shape
        qimg = QImage(frame.data, w, h, frame.strides[0], QImage.Format.Format_Grayscale8).copy()
        pix = QPixmap.fromImage(qimg).scaled(
            self.phase_media_dialog_preview.width(),
            self.phase_media_dialog_preview.height(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.phase_media_dialog_preview.setPixmap(pix)

    def _toggle_phase_media_play(self):
        if not self.phase_media_frames:
            QMessageBox.information(self, "Phase Media", "Load video/folder sequence first.")
            return
        if self.phase_media_timer.isActive():
            self._stop_phase_media()
            return
        fps = max(1, int(self.phase_media_fps.value()))
        self.phase_media_timer.start(max(1, int(round(1000.0 / fps))))
        self.phase_media_play_btn.setText("Pause Media")
        self.statusBar().showMessage("Phase media playback started.", 2000)
        self._ensure_phase_media_dialog()
        self._update_phase_media_dialog_ui()
        self.phase_media_dialog.show()

    def _stop_phase_media(self):
        self.phase_media_timer.stop()
        self.phase_media_play_btn.setText("Play Media")
        self.statusBar().showMessage("Phase media playback stopped.", 2000)

    def _on_phase_media_timer(self):
        if not self.phase_media_frames:
            self._stop_phase_media()
            return
        self.phase_media_index = (self.phase_media_index + 1) % len(self.phase_media_frames)
        frame = self.phase_media_frames[self.phase_media_index]
        self.current_mask = frame
        self._refresh_mask_view()
        self.phase_media_info.setText(
            f"Media frame: {self.phase_media_index + 1}/{len(self.phase_media_frames)}"
        )
        self._update_phase_media_dialog_ui()
        if self.phase_media_send_slm.isChecked():
            self._send_mask_to_output(frame)

    def _manual_add_point(self):
        row = self.manual_table.rowCount()
        self.manual_table.insertRow(row)
        self.manual_table.setItem(row, 0, QTableWidgetItem(str(row + 1)))
        self.manual_table.setItem(row, 1, QTableWidgetItem("0"))
        self.manual_table.setItem(row, 2, QTableWidgetItem("0"))

    def _manual_clear_points(self):
        self.manual_table.setRowCount(0)
        self.camera_table.setRowCount(0)
        self.current_points = []
        self._refresh_all_views()
        self.statusBar().showMessage("All target points cleared.", 3000)

    def _manual_apply_points(self):
        points = []
        for row in range(self.manual_table.rowCount()):
            x_item = self.manual_table.item(row, 1)
            y_item = self.manual_table.item(row, 2)
            if x_item is None or y_item is None:
                continue
            try:
                points.append((float(x_item.text()), float(y_item.text())))
            except ValueError:
                continue
        self.current_points = points
        self._sync_camera_table(points)
        self._refresh_target_view()
        self._refresh_camera_view()
        self.statusBar().showMessage(f"Loaded {len(points)} manual points.", 3000)

    def _sync_camera_table(self, points):
        self.camera_table.setRowCount(0)
        for idx, (x, y) in enumerate(points, start=1):
            row = self.camera_table.rowCount()
            self.camera_table.insertRow(row)
            self.camera_table.setItem(row, 0, QTableWidgetItem(str(idx)))
            self.camera_table.setItem(row, 1, QTableWidgetItem(f"{x:.3f}"))
            self.camera_table.setItem(row, 2, QTableWidgetItem(f"{y:.3f}"))

    def _pattern_generate_points(self):
        req = {
            "preset": self.pattern_combo.currentText(),
            "point_count": self.p_count.value(),
            "radius": self.p_radius.value(),
            "size": self.p_size.value(),
            "width": self.p_width.value(),
            "height": self.p_height.value(),
            "rotation_deg": self.p_rot.value(),
            "x_shift": self.p_x.value(),
            "y_shift": self.p_y.value(),
            "star_points": self.p_star.value(),
            "inner_radius": self.p_inner.value(),
            "moon_radius": self.p_moon.value(),
            "distance": self.p_dist.value(),
            "grid_rows": self.p_rows.value(),
            "grid_cols": self.p_cols.value(),
            "row_spacing": self.p_rs.value(),
            "col_spacing": self.p_cs.value(),
        }
        self.current_points = generate_pattern(req)
        self._sync_manual_table(self.current_points)
        self._sync_camera_table(self.current_points)
        self._refresh_target_view()
        self._refresh_camera_view()
        self.statusBar().showMessage(f"Pattern generated with {len(self.current_points)} points.", 3000)

    def _sync_manual_table(self, points):
        self.manual_table.setRowCount(0)
        for idx, (x, y) in enumerate(points, start=1):
            row = self.manual_table.rowCount()
            self.manual_table.insertRow(row)
            self.manual_table.setItem(row, 0, QTableWidgetItem(str(idx)))
            self.manual_table.setItem(row, 1, QTableWidgetItem(f"{x:.3f}"))
            self.manual_table.setItem(row, 2, QTableWidgetItem(f"{y:.3f}"))

    def _load_target_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Target Image", "", "Images (*.png *.bmp *.jpg *.jpeg *.tif *.tiff)"
        )
        if not path:
            return
        arr = np.asarray(Image.open(path).convert("L"))
        ys, xs = np.where(arr > 200)
        if len(xs) == 0:
            QMessageBox.warning(self, "Image", "No bright pixels found above threshold.")
            return
        if len(xs) > 2000:
            picks = np.linspace(0, len(xs) - 1, 2000).astype(np.int32)
            xs = xs[picks]
            ys = ys[picks]
        cx = arr.shape[1] / 2.0
        cy = arr.shape[0] / 2.0
        self.current_points = [(float(x - cx), float(cy - y)) for x, y in zip(xs, ys)]
        self._sync_manual_table(self.current_points)
        self._sync_camera_table(self.current_points)
        self.image_info.setText(f"Loaded {Path(path).name}: {len(self.current_points)} points")
        self._refresh_target_view()
        self._refresh_camera_view()

    def _clear_target_image(self):
        self.image_info.setText("No image loaded. Will convert bright pixels into target points.")
        self.current_points = []
        self._sync_manual_table([])
        self._sync_camera_table([])
        self._refresh_target_view()
        self._refresh_camera_view()

    def _run_python_code(self):
        try:
            result = run_python_trap_script(
                self.python_editor.toPlainText(),
                self.python_frames_spin.value(),
                int(self.config.get("cam_width", 1920)),
                int(self.config.get("cam_height", 1080)),
            )
        except Exception as exc:  # noqa: BLE001
            QMessageBox.critical(self, "Python Script Error", str(exc))
            return
        self.python_frames = result["frames"]
        if result.get("fps"):
            self.python_fps_spin.setValue(int(result["fps"]))
        self.python_frame_index = 0
        self.python_playing = False
        self.python_stop_btn.setEnabled(False)
        self._populate_python_trap_selector()
        self.python_info.setText(f"Script OK: {result['frame_count']} frames @ {self.python_fps_spin.value()} FPS")
        self.statusBar().showMessage("Python sequence generated.", 3000)

    def _populate_python_trap_selector(self):
        self.python_trap_selector.blockSignals(True)
        self.python_trap_selector.clear()
        self.python_trap_selector.addItem("None", -1)
        if self.python_frames and self.python_frames[0]:
            for i in range(len(self.python_frames[0])):
                self.python_trap_selector.addItem(f"Trap {i + 1}", i)
        self.python_trap_selector.blockSignals(False)

    def _play_python_sequence(self):
        if not self.python_frames:
            QMessageBox.information(self, "Python", "Run Code first.")
            return
        self.python_playing = True
        self.python_stop_btn.setEnabled(True)
        interval_ms = max(1, int(round(1000.0 / max(1, self.python_fps_spin.value()))))
        self.python_timer.start(interval_ms)
        self._on_python_timer()

    def _stop_python_sequence(self):
        self.python_playing = False
        self.python_timer.stop()
        self.python_stop_btn.setEnabled(False)

    def _reset_python_sequence(self):
        self._stop_python_sequence()
        self.python_frame_index = 0
        if self.python_frames:
            self.current_points = list(self.python_frames[0])
            self._sync_manual_table(self.current_points)
            self._sync_camera_table(self.current_points)
            self._refresh_target_view()
            self._refresh_camera_view()

    def _on_python_timer(self):
        if not self.python_playing or not self.python_frames:
            return
        idx = self.python_frame_index % len(self.python_frames)
        frame_points = list(self.python_frames[idx])
        trap_idx = int(self.python_trap_selector.currentData())
        if trap_idx >= 0 and trap_idx < len(frame_points):
            frame_points = [frame_points[trap_idx]]
        self.current_points = frame_points
        self._sync_manual_table(self.current_points)
        self._sync_camera_table(self.current_points)
        self._refresh_target_view()
        self._refresh_camera_view()
        if self.python_realtime_cb.isChecked():
            try:
                mask, _ = generate_phase_mask(self.current_points, self._settings_dict(), self._algorithm_name())
                self.current_mask = mask
                self._refresh_mask_view()
            except Exception:
                pass
        self.python_frame_index = (idx + 1) % len(self.python_frames)

    def _save_python_script(self):
        path, _ = QFileDialog.getSaveFileName(self, "Save Python Script", "trap_script.py", "Python (*.py)")
        if not path:
            return
        Path(path).write_text(self.python_editor.toPlainText(), encoding="utf-8")
        self.statusBar().showMessage(f"Script saved: {path}", 3000)

    def _load_python_script(self):
        path, _ = QFileDialog.getOpenFileName(self, "Load Python Script", "", "Python (*.py)")
        if not path:
            return
        self.python_editor.setPlainText(Path(path).read_text(encoding="utf-8"))
        self.statusBar().showMessage(f"Script loaded: {path}", 3000)

    def _generate_mask_clicked(self):
        try:
            mask, info = generate_phase_mask(self.current_points, self._settings_dict(), self._algorithm_name())
        except Exception as exc:  # noqa: BLE001
            QMessageBox.warning(self, "Generate Mask", str(exc))
            return
        self.current_mask = mask
        self._refresh_mask_view()
        self.statusBar().showMessage(
            f"Mask generated with {info['used']}/{info['requested']} mapped points "
            f"(skip cam={info['skipped_camera']}, skip slm={info['skipped_slm']}).",
            5000,
        )

    def _load_phase_mask_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Phase Mask Image", "", "Images (*.png *.bmp *.jpg *.jpeg *.tif *.tiff)"
        )
        if not path:
            return
        self.phase_media_timer.stop()
        self.phase_media_play_btn.setText("Play Media")
        self.phase_media_frames = []
        self.phase_media_index = 0
        arr = np.asarray(Image.open(path).convert("L"), dtype=np.uint8)
        self.current_mask = arr
        self._refresh_mask_view()
        self.phase_media_info.setText(f"Media: {Path(path).name} (single image)")
        self.statusBar().showMessage(f"Mask loaded: {Path(path).name}", 3000)

    def _compose_final_mask(self):
        if self.current_mask is None and self.correction_mask is None:
            return None
        if self.current_mask is None:
            return np.array(self.correction_mask, copy=True)
        out = np.array(self.current_mask, copy=True)
        if self.correction_mask is not None and self.preview_corr_cb.isChecked():
            corr = self.correction_mask
            if corr.shape != out.shape:
                corr = np.asarray(Image.fromarray(corr).resize((out.shape[1], out.shape[0])), dtype=np.uint8)
            out = ((out.astype(np.uint16) + corr.astype(np.uint16)) % 256).astype(np.uint8)
        return out

    def _on_save_mask(self):
        final = self._compose_final_mask()
        if final is None:
            QMessageBox.information(self, "Save Phase Mask", "No mask to save.")
            return
        path, _ = QFileDialog.getSaveFileName(self, "Save Phase Mask", "phase_mask.png", "Images (*.png *.bmp *.jpg)")
        if not path:
            return
        Image.fromarray(final, mode="L").save(path)
        self.statusBar().showMessage(f"Phase mask saved: {path}", 4000)

    def _on_load_correction(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Flatness Correction Mask", "", "Images (*.png *.bmp *.jpg *.jpeg *.tif *.tiff)"
        )
        if not path:
            return
        base_w = int(self.config.get("slm_width", 1920))
        base_h = int(self.config.get("slm_height", 1080))
        self.correction_mask = np.asarray(Image.open(path).convert("L").resize((base_w, base_h)), dtype=np.uint8)
        self._refresh_mask_view()
        self.statusBar().showMessage("Correction Mask loaded.", 4000)

    def _on_clear_correction(self):
        self.correction_mask = None
        self._refresh_mask_view()
        self.statusBar().showMessage("SLM correction cleared.", 3000)

    def _send_to_slm(self):
        final = self._compose_final_mask()
        if final is None:
            QMessageBox.warning(self, "SLM", "No mask or correction loaded to send.")
            return
        ok, msg = self._send_mask_to_output(final)
        if not ok:
            QMessageBox.warning(self, "SLM Output", msg)
            return
        if self.current_mask is None and self.correction_mask is not None:
            self.statusBar().showMessage("Background Correction Mask sent to SLM.", 5000)
        elif self.current_mask is not None and self.correction_mask is not None:
            self.statusBar().showMessage("Phase Mask + Correction sent to SLM.", 5000)
        else:
            self.statusBar().showMessage("Phase Mask sent to SLM.", 5000)

    def _clear_slm(self):
        self.current_mask = None
        self._refresh_mask_view()
        if self.correction_mask is not None:
            final = self._compose_final_mask()
            ok, msg = self._send_mask_to_output(final)
            if ok:
                self.statusBar().showMessage("Target cleared. SLM correction remains active.", 5000)
            else:
                QMessageBox.warning(self, "SLM Output", msg)
            return
        self._clear_slm_output()
        self.statusBar().showMessage("SLM display completely terminated.", 3000)

    def _refresh_camera_list(self):
        if self.camera_backend == 2:
            self.cam_select.clear()
            self.cam_select.addItem(f"UDP ({self.udp_bind_ip}:{self.udp_port})", -1)
            self.statusBar().showMessage("Camera list set to UDP feed mode.", 3000)
            return
        current_data = self.cam_select.currentData()
        self.cam_select.clear()
        cameras = self.camera_manager.discover_cameras(max_index=8)
        if not cameras:
            self.cam_select.addItem("No camera found", -1)
            self.statusBar().showMessage("No camera devices found.", 3000)
            return
        selected_row = 0
        for i, cam in enumerate(cameras):
            self.cam_select.addItem(cam["name"], cam["index"])
            if cam["index"] == current_data:
                selected_row = i
        self.cam_select.setCurrentIndex(selected_row)
        self.statusBar().showMessage(f"Camera list refreshed ({len(cameras)} found).", 3000)

    def _start_camera_feed(self):
        if self.camera_backend == 2:
            ok, msg = self.camera_manager.start_udp(self.udp_bind_ip, self.udp_port)
            if not ok:
                QMessageBox.warning(self, "UDP Camera Feed", msg)
                return
            self.camera_timer.start()
            self.statusBar().showMessage(msg, 3000)
            return
        index = int(self.cam_select.currentData())
        if index < 0:
            QMessageBox.warning(self, "Camera", "No camera selected.")
            return
        ok, msg = self.camera_manager.start(index, use_default_backend=(self.camera_backend == 0))
        if not ok:
            QMessageBox.warning(self, "Camera", msg)
            return
        self.camera_timer.start()
        self.statusBar().showMessage(msg, 3000)

    def _stop_camera_feed(self):
        self.camera_timer.stop()
        if self.camera_manager.is_recording():
            self.camera_manager.stop_recording()
        self.record_video_btn.blockSignals(True)
        self.record_video_btn.setChecked(False)
        self.record_video_btn.setText("Record Video")
        self.record_video_btn.blockSignals(False)
        self.camera_manager.stop()
        self.camera_frame_rgb = None
        self.camera_source_size = None
        self.camera_draw_rect = QRect()
        self.last_fps_timestamp_ms = 0.0
        self.last_fps_frame_count = 0
        self.camera_view.setPixmap(QPixmap())
        self.camera_view.setText("Camera feed offline")
        self.camera_pixel_label.setText("Pixel: --, -- | RGB: --, --, --")
        if self.camera_preview_fps_label is not None:
            self.camera_preview_fps_label.setText("FPS: 0")
        if self.camera_preview_pixel_label is not None:
            self.camera_preview_pixel_label.setText("Camera: --, -- | I: --")

    def _on_camera_timer(self):
        frame_bgr = self.camera_manager.read_frame()
        if frame_bgr is None:
            self.statusBar().showMessage("Camera frame read failed.", 2000)
            return
        self._render_camera_frame(frame_bgr)
        now_ms = time.time() * 1000.0
        if self.last_fps_timestamp_ms == 0.0:
            self.last_fps_timestamp_ms = now_ms
            self.last_fps_frame_count = 0
        self.last_fps_frame_count += 1
        elapsed = now_ms - self.last_fps_timestamp_ms
        if elapsed >= 1000.0:
            fps = self.last_fps_frame_count * 1000.0 / max(1.0, elapsed)
            if self.camera_preview_fps_label is not None:
                self.camera_preview_fps_label.setText(f"FPS: {fps:.1f}")
            self.last_fps_timestamp_ms = now_ms
            self.last_fps_frame_count = 0
        if self.camera_manager.is_recording():
            self.camera_manager.write_record_frame(frame_bgr)

    def _render_camera_frame(self, frame_bgr):
        display_bgr = np.array(frame_bgr, copy=True)
        if self.overlay_target_cb.isChecked():
            self._draw_overlay_on_frame(display_bgr)

        rgb = cv2.cvtColor(display_bgr, cv2.COLOR_BGR2RGB)
        self.camera_frame_rgb = rgb
        h, w = rgb.shape[:2]
        self.camera_source_size = (w, h)

        qimg = QImage(rgb.data, w, h, rgb.strides[0], QImage.Format.Format_RGB888).copy()
        pixmap = QPixmap.fromImage(qimg)
        scaled = pixmap.scaled(
            self.camera_view.width(),
            self.camera_view.height(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.camera_view.setPixmap(scaled)
        self.camera_draw_rect = self._pixmap_draw_rect(self.camera_view, scaled)
        if self.camera_preview_window is not None and self.camera_preview_window.isVisible() and self.camera_preview_label is not None:
            preview_scaled = pixmap.scaled(
                self.camera_preview_label.width(),
                self.camera_preview_label.height(),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
            self.camera_preview_label.setPixmap(preview_scaled)

    def _draw_overlay_on_frame(self, frame_bgr):
        h, w = frame_bgr.shape[:2]
        cam_w = max(1.0, float(self.config.get("cam_width", w)))
        cam_h = max(1.0, float(self.config.get("cam_height", h)))
        for x, y in self.current_points:
            px = int(round((x / cam_w + 0.5) * w))
            py = int(round((0.5 - y / cam_h) * h))
            if 0 <= px < w and 0 <= py < h:
                cv2.circle(frame_bgr, (px, py), 3, (20, 220, 120), -1, lineType=cv2.LINE_AA)

    def _pixmap_draw_rect(self, label, pixmap):
        lw = label.width()
        lh = label.height()
        pw = pixmap.width()
        ph = pixmap.height()
        x = (lw - pw) // 2
        y = (lh - ph) // 2
        return QRect(x, y, pw, ph)

    def _update_camera_pixel_readout(self, pos: QPoint, source_widget=None):
        if self.camera_frame_rgb is None or self.camera_source_size is None:
            self.camera_pixel_label.setText("Pixel: --, -- | RGB: --, --, --")
            if self.camera_preview_pixel_label is not None:
                self.camera_preview_pixel_label.setText("Camera: --, -- | I: --")
            return
        widget = source_widget if source_widget is not None else self.camera_view
        rect = self.camera_draw_rect
        if widget == self.camera_preview_label and self.camera_preview_label is not None and self.camera_preview_label.pixmap() is not None:
            rect = self._pixmap_draw_rect(self.camera_preview_label, self.camera_preview_label.pixmap())
        if not rect.contains(pos):
            self.camera_pixel_label.setText("Pixel: --, -- | RGB: --, --, --")
            if self.camera_preview_pixel_label is not None:
                self.camera_preview_pixel_label.setText("Camera: --, -- | I: --")
            return
        sw, sh = self.camera_source_size
        rx = (pos.x() - rect.x()) / max(1, rect.width())
        ry = (pos.y() - rect.y()) / max(1, rect.height())
        ix = int(np.clip(round(rx * (sw - 1)), 0, sw - 1))
        iy = int(np.clip(round(ry * (sh - 1)), 0, sh - 1))
        r, g, b = self.camera_frame_rgb[iy, ix].tolist()
        self.camera_pixel_label.setText(f"Pixel: {ix}, {iy} | RGB: {r}, {g}, {b}")
        intensity = int(round((int(r) + int(g) + int(b)) / 3.0))
        if self.camera_preview_pixel_label is not None:
            self.camera_preview_pixel_label.setText(f"Camera: {ix}, {iy} | I: {intensity}")

    def _save_camera_snapshot(self):
        if self.camera_manager.last_frame_bgr is None:
            QMessageBox.information(self, "Camera Snapshot", "No live frame available.")
            return
        default_name = f"capture_{datetime.now().strftime('%Y%m%d_%H%M%S')}.png"
        path, _ = QFileDialog.getSaveFileName(self, "Save Camera Snapshot", default_name, "Images (*.png *.jpg *.bmp)")
        if not path:
            return
        if self.camera_manager.save_snapshot(path):
            self.statusBar().showMessage(f"Snapshot saved: {path}", 4000)
        else:
            QMessageBox.warning(self, "Camera Snapshot", "Failed to save snapshot.")

    def _toggle_recording(self, checked):
        if self.camera_preview_record_btn is not None and self.sender() != self.camera_preview_record_btn:
            self.camera_preview_record_btn.blockSignals(True)
            self.camera_preview_record_btn.setChecked(checked)
            self.camera_preview_record_btn.blockSignals(False)
        if self.record_video_btn is not None and self.sender() != self.record_video_btn:
            self.record_video_btn.blockSignals(True)
            self.record_video_btn.setChecked(checked)
            self.record_video_btn.blockSignals(False)
        if checked:
            if self.camera_manager.last_frame_bgr is None:
                QMessageBox.warning(self, "Record", "Start camera feed before recording.")
                self.record_video_btn.blockSignals(True)
                self.record_video_btn.setChecked(False)
                self.record_video_btn.blockSignals(False)
                return
            default_name = f"record_{datetime.now().strftime('%Y%m%d_%H%M%S')}.mp4"
            path, _ = QFileDialog.getSaveFileName(self, "Record Camera Video", default_name, "Video (*.mp4)")
            if not path:
                self.record_video_btn.blockSignals(True)
                self.record_video_btn.setChecked(False)
                self.record_video_btn.blockSignals(False)
                return
            ok, msg = self.camera_manager.start_recording(path, fps=30)
            if not ok:
                QMessageBox.warning(self, "Record", msg)
                self.record_video_btn.blockSignals(True)
                self.record_video_btn.setChecked(False)
                self.record_video_btn.blockSignals(False)
                return
            self.record_video_btn.setText("Stop Recording")
            if self.camera_preview_record_btn is not None:
                self.camera_preview_record_btn.setText("Stop Recording")
            self.statusBar().showMessage(msg, 3000)
            return

        if self.camera_manager.is_recording():
            self.camera_manager.stop_recording()
            self.statusBar().showMessage("Recording stopped.", 3000)
        self.record_video_btn.setText("Record Video")
        if self.camera_preview_record_btn is not None:
            self.camera_preview_record_btn.setText("Record Video")

    def _target_draw_rect(self):
        pix = self.target_view.pixmap()
        if pix is None or pix.isNull():
            return QRect()
        lw = self.target_view.width()
        lh = self.target_view.height()
        pw = pix.width()
        ph = pix.height()
        return QRect((lw - pw) // 2, (lh - ph) // 2, pw, ph)

    def _target_point_to_label(self, x, y):
        rect = self._target_draw_rect()
        if rect.width() <= 0 or rect.height() <= 0:
            return None
        cam_w = max(1.0, float(self.config.get("cam_width", 1920)) / 2.0)
        cam_h = max(1.0, float(self.config.get("cam_height", 1080)) / 2.0)
        px = int(round(rect.x() + rect.width() / 2.0 + (x / cam_w) * (rect.width() / 2.0 - 10)))
        py = int(round(rect.y() + rect.height() / 2.0 - (y / cam_h) * (rect.height() / 2.0 - 10)))
        return QPoint(px, py)

    def _label_to_target_point(self, pos):
        rect = self._target_draw_rect()
        if rect.width() <= 0 or rect.height() <= 0:
            return None
        if not rect.contains(pos):
            return None
        cam_w = max(1.0, float(self.config.get("cam_width", 1920)) / 2.0)
        cam_h = max(1.0, float(self.config.get("cam_height", 1080)) / 2.0)
        nx = (pos.x() - (rect.x() + rect.width() / 2.0)) / max(1.0, (rect.width() / 2.0 - 10))
        ny = ((rect.y() + rect.height() / 2.0) - pos.y()) / max(1.0, (rect.height() / 2.0 - 10))
        return float(nx * cam_w), float(ny * cam_h)

    def _nearest_point_index(self, pos, radius_px=10):
        best_idx = -1
        best_d2 = radius_px * radius_px
        for i, (x, y) in enumerate(self.current_points):
            pp = self._target_point_to_label(x, y)
            if pp is None:
                continue
            dx = pp.x() - pos.x()
            dy = pp.y() - pos.y()
            d2 = dx * dx + dy * dy
            if d2 <= best_d2:
                best_d2 = d2
                best_idx = i
        return best_idx

    def _on_target_mouse_press(self, pos, button):
        if button == Qt.MouseButton.LeftButton:
            idx = self._nearest_point_index(pos)
            if idx >= 0:
                self.selected_point_index = idx
                self.dragging_point = True
                return
            coords = self._label_to_target_point(pos)
            if coords is None:
                return
            self.current_points.append(coords)
            self.selected_point_index = len(self.current_points) - 1
            self._sync_manual_table(self.current_points)
            self._sync_camera_table(self.current_points)
            self._refresh_target_view()
            self._refresh_camera_view()
        elif button == Qt.MouseButton.RightButton:
            idx = self._nearest_point_index(pos)
            if idx >= 0:
                self.current_points.pop(idx)
                self.selected_point_index = -1
                self._sync_manual_table(self.current_points)
                self._sync_camera_table(self.current_points)
                self._refresh_target_view()
                self._refresh_camera_view()

    def _on_target_mouse_release(self, _pos, button):
        if button == Qt.MouseButton.LeftButton:
            self.dragging_point = False

    def _update_grid_hover_and_drag(self, pos):
        coords = self._label_to_target_point(pos)
        if coords is None:
            self.grid_hover_label.setText("Grid: --, --")
            return
        self.grid_hover_label.setText(f"Grid: {coords[0]:.1f}, {coords[1]:.1f}")
        if self.dragging_point and 0 <= self.selected_point_index < len(self.current_points):
            self.current_points[self.selected_point_index] = coords
            self._sync_manual_table(self.current_points)
            self._sync_camera_table(self.current_points)
            self._refresh_target_view()
            self._refresh_camera_view()

    def _refresh_all_views(self):
        self._refresh_target_view()
        self._refresh_mask_view()
        self._refresh_camera_view()

    def _refresh_target_view(self):
        w, h = 600, 360
        canvas = np.zeros((h, w, 3), dtype=np.uint8)
        cx, cy = w // 2, h // 2
        canvas[:, cx : cx + 1] = (40, 40, 40)
        canvas[cy : cy + 1, :] = (40, 40, 40)
        cam_w = max(1.0, float(self.config.get("cam_width", 1920)) / 2.0)
        cam_h = max(1.0, float(self.config.get("cam_height", 1080)) / 2.0)
        for i, (x, y) in enumerate(self.current_points):
            px = int(round(cx + (x / cam_w) * (w / 2.0 - 10)))
            py = int(round(cy - (y / cam_h) * (h / 2.0 - 10)))
            if 0 <= px < w and 0 <= py < h:
                color = (0, 220, 255)
                if i == self.selected_point_index:
                    color = (255, 220, 0)
                canvas[max(0, py - 3) : min(h, py + 4), max(0, px - 3) : min(w, px + 4)] = color
        qimg = QImage(canvas.data, w, h, canvas.strides[0], QImage.Format.Format_RGB888)
        self.target_view.setPixmap(QPixmap.fromImage(qimg.copy()))

    def _refresh_mask_view(self):
        final = self._compose_final_mask()
        if final is None:
            self.phase_mask_view.setPixmap(QPixmap())
            self.phase_mask_view.setText("No mask")
            return
        h, w = final.shape
        qimg = QImage(final.data, w, h, final.strides[0], QImage.Format.Format_Grayscale8)
        pix = QPixmap.fromImage(qimg.copy()).scaled(
            self.phase_mask_view.width(),
            self.phase_mask_view.height(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation,
        )
        self.phase_mask_view.setPixmap(pix)

    def _refresh_camera_view(self):
        if self.camera_manager.last_frame_bgr is not None:
            self._render_camera_frame(self.camera_manager.last_frame_bgr)
            return
        if not self.overlay_target_cb.isChecked():
            return
        w, h = 600, 360
        canvas = np.zeros((h, w, 3), dtype=np.uint8)
        canvas[:] = (18, 18, 18)
        cx, cy = w // 2, h // 2
        cam_w = max(1.0, float(self.config.get("cam_width", 1920)) / 2.0)
        cam_h = max(1.0, float(self.config.get("cam_height", 1080)) / 2.0)
        for x, y in self.current_points:
            px = int(round(cx + (x / cam_w) * (w / 2.0 - 10)))
            py = int(round(cy - (y / cam_h) * (h / 2.0 - 10)))
            if 0 <= px < w and 0 <= py < h:
                canvas[max(0, py - 2) : min(h, py + 3), max(0, px - 2) : min(w, px + 3)] = (0, 190, 120)
        qimg = QImage(canvas.data, w, h, canvas.strides[0], QImage.Format.Format_RGB888)
        self.camera_view.setPixmap(QPixmap.fromImage(qimg.copy()))

    def _apply_theme(self, dark):
        if dark:
            self.setStyleSheet(
                "QMainWindow{background:#1f2228;color:#e5e7eb;}"
                "QWidget{color:#e5e7eb;}"
                "QGroupBox{border:1px solid #4b5563; margin-top:8px; padding-top:8px;}"
                "QPushButton{background:#374151; border:1px solid #6b7280; padding:4px 8px;}"
                "QLineEdit,QTextEdit,QSpinBox,QDoubleSpinBox,QComboBox,QTableWidget{background:#111827; border:1px solid #4b5563;}"
                "QTabWidget::pane{border:1px solid #4b5563;}"
            )
        else:
            self.setStyleSheet("")

    def _toggle_theme(self):
        self.is_dark_mode = not self.is_dark_mode
        self._apply_theme(self.is_dark_mode)

    def _on_monitor_selected(self, monitor_number):
        self.selected_monitor_number = int(monitor_number)
        self.config["slm_selected_monitor"] = self.selected_monitor_number
        self._refresh_monitor_menu()
        if self.direct_output_window is not None and self.direct_output_window.isVisible():
            screen = self._selected_screen()
            if screen is not None:
                self.direct_output_window.setGeometry(screen.geometry())
        self.statusBar().showMessage(f"Selected monitor {monitor_number}.", 2000)

    def _refresh_monitor_menu(self):
        if not hasattr(self, "monitor_menu") or self.monitor_menu is None:
            return
        self.monitor_menu.clear()
        screens = QGuiApplication.screens()
        if not screens:
            act = QAction("No monitor detected", self)
            act.setEnabled(False)
            self.monitor_menu.addAction(act)
            return
        for idx, screen in enumerate(screens, start=1):
            name = screen.name() if screen.name() else f"Display {idx}"
            act = QAction(f"{idx}: {name}", self)
            act.setCheckable(True)
            act.setChecked(idx == self.selected_monitor_number)
            act.triggered.connect(lambda _checked=False, n=idx: self._on_monitor_selected(n))
            self.monitor_menu.addAction(act)
        self._refresh_camera_preview_monitor_options()

    def _refresh_camera_preview_monitor_options(self):
        if not hasattr(self, "camera_preview_monitor_combo"):
            return
        screens = QGuiApplication.screens()
        self.camera_preview_monitor_combo.blockSignals(True)
        self.camera_preview_monitor_combo.clear()
        if not screens:
            self.camera_preview_monitor_combo.addItem("No monitor", -1)
            self.camera_preview_monitor_combo.blockSignals(False)
            return
        for idx, screen in enumerate(screens, start=1):
            name = screen.name() if screen.name() else f"Display {idx}"
            self.camera_preview_monitor_combo.addItem(f"{idx}: {name}", idx)
        selected = max(1, min(self.camera_preview_monitor_number, len(screens)))
        self.camera_preview_monitor_combo.setCurrentIndex(selected - 1)
        self.camera_preview_monitor_combo.blockSignals(False)

    def _on_camera_preview_monitor_changed(self, _index):
        value = self.camera_preview_monitor_combo.currentData()
        if value is None:
            return
        self.camera_preview_monitor_number = int(value)
        self.config["camera_preview_monitor"] = int(value)
        if self.camera_preview_window is not None and self.camera_preview_window.isVisible():
            screens = QGuiApplication.screens()
            idx = max(1, min(self.camera_preview_monitor_number, len(screens))) - 1
            self.camera_preview_window.setGeometry(screens[idx].geometry())

    def _ensure_camera_preview_window(self):
        if self.camera_preview_window is not None:
            return
        self.camera_preview_window = QWidget(None, Qt.WindowType.Window)
        self.camera_preview_window.setWindowTitle("Camera Preview")
        layout = QVBoxLayout(self.camera_preview_window)
        layout.setContentsMargins(4, 4, 4, 4)
        self.camera_preview_label = QLabel("Camera Feed (Offline)")
        self.camera_preview_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.camera_preview_label.setMinimumSize(640, 360)
        self.camera_preview_label.setStyleSheet("border:1px solid #666; background:#0d0d0d;")
        self.camera_preview_label.setMouseTracking(True)
        self.camera_preview_label.installEventFilter(self)
        layout.addWidget(self.camera_preview_label, 1)
        controls = QHBoxLayout()
        self.camera_preview_save_btn = QPushButton("Save Image")
        self.camera_preview_save_btn.clicked.connect(self._save_camera_snapshot)
        controls.addWidget(self.camera_preview_save_btn)
        self.camera_preview_record_btn = QPushButton("Record Video")
        self.camera_preview_record_btn.setCheckable(True)
        self.camera_preview_record_btn.toggled.connect(self._toggle_recording)
        controls.addWidget(self.camera_preview_record_btn)
        self.camera_preview_record_time_label = QLabel("00:00")
        controls.addWidget(self.camera_preview_record_time_label)
        self.camera_preview_fps_label = QLabel("FPS: 0")
        controls.addWidget(self.camera_preview_fps_label)
        self.camera_preview_pixel_label = QLabel("Camera: --, -- | I: --")
        controls.addWidget(self.camera_preview_pixel_label, 1)
        layout.addLayout(controls)

    def _on_camera_preview_toggled(self, checked):
        if not checked:
            if self.camera_preview_window is not None:
                self.camera_preview_window.hide()
            self.camera_preview_toggle_btn.setText("Show On Monitor")
            return
        self._ensure_camera_preview_window()
        screens = QGuiApplication.screens()
        if not screens:
            self.camera_preview_toggle_btn.blockSignals(True)
            self.camera_preview_toggle_btn.setChecked(False)
            self.camera_preview_toggle_btn.blockSignals(False)
            return
        idx = max(1, min(self.camera_preview_monitor_number, len(screens))) - 1
        self.camera_preview_window.setGeometry(screens[idx].geometry())
        self.camera_preview_window.show()
        self.camera_preview_window.raise_()
        self.camera_preview_toggle_btn.setText("Hide Monitor")

    def _selected_screen(self):
        screens = QGuiApplication.screens()
        if not screens:
            return None
        idx = max(1, min(self.selected_monitor_number, len(screens))) - 1
        return screens[idx]

    def _ensure_direct_output_window(self):
        if self.direct_output_window is not None:
            return
        self.direct_output_window = QWidget(None, Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint)
        self.direct_output_window.setWindowTitle("HOT Direct Output")
        layout = QVBoxLayout(self.direct_output_window)
        layout.setContentsMargins(0, 0, 0, 0)
        self.direct_output_label = QLabel(self.direct_output_window)
        self.direct_output_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.direct_output_label.setStyleSheet("background: black;")
        self.direct_output_label.setScaledContents(True)
        layout.addWidget(self.direct_output_label)

    def _display_direct_output(self, mask):
        self._ensure_direct_output_window()
        screen = self._selected_screen()
        if screen is None:
            return False, "No display detected for direct output."
        h, w = mask.shape
        qimg = QImage(mask.data, w, h, mask.strides[0], QImage.Format.Format_Grayscale8).copy()
        self.direct_output_label.setPixmap(QPixmap.fromImage(qimg))
        self.direct_output_window.setGeometry(screen.geometry())
        self.direct_output_window.show()
        self.direct_output_window.raise_()
        return True, "Mask displayed on direct output."

    def _clear_direct_output(self):
        if self.direct_output_window is not None:
            self.direct_output_window.hide()

    def _send_mask_to_output(self, mask):
        if mask is None:
            return False, "No mask to send."
        if self.slm_output_mode == "dll":
            ok, msg = self.slm_dll.send_mask(mask, self.selected_monitor_number, self.slm_window_id)
            if not ok:
                self._update_slm_output_label()
            return ok, msg
        return self._display_direct_output(mask)

    def _clear_slm_output(self):
        if self.slm_output_mode == "dll":
            ok, _msg = self.slm_dll.clear(self.slm_window_id)
            if ok:
                return
        self._clear_direct_output()

    def _show_about(self):
        QMessageBox.about(
            self,
            "About Holographic Optical Tweezer Control",
            "Holographic Optical Tweezer Control\n\nPython port in progress.\nUI/function parity is being added step-by-step.",
        )

    def _show_not_implemented(self, name):
        QMessageBox.information(self, "Planned", f"{name} is planned in upcoming parity steps.")
