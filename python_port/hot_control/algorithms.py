from __future__ import annotations

import math
import numpy as np

TWO_PI = 2.0 * math.pi


def gaussian_source(width, height, beam_waist_px=None):
    if beam_waist_px is None or beam_waist_px <= 0:
        beam_waist_px = min(width, height) / 6.0
    y, x = np.indices((height, width), dtype=float)
    cx = (width - 1) / 2.0
    cy = (height - 1) / 2.0
    sigma = max(float(beam_waist_px), 1e-6)
    r2 = (x - cx) ** 2 + (y - cy) ** 2
    return np.exp(-r2 / (2.0 * sigma * sigma)).astype(np.float32)


def mapping_values(settings):
    slm_dx = settings["slm_pixel_size_um"] * 1e-6
    slm_dy = settings["slm_pixel_size_um"] * 1e-6
    cam_um = settings["cam_pixel_size_um"] / max(settings["camera_imaging_magnification"], 1e-6)
    cam_dx = cam_um * 1e-6
    cam_dy = cam_um * 1e-6
    wavelength = settings["wavelength_nm"] * 1e-9
    focal_length = settings["focal_length_mm"] * 1e-3
    focal_dx = wavelength * focal_length / (settings["slm_width"] * slm_dx)
    focal_dy = wavelength * focal_length / (settings["slm_height"] * slm_dy)
    return cam_dx, cam_dy, focal_dx, focal_dy


def points_to_target_amplitude(points, settings):
    points = list(points)
    target = np.zeros((settings["slm_height"], settings["slm_width"]), dtype=np.float32)
    cam_dx, cam_dy, focal_dx, focal_dy = mapping_values(settings)
    cx = settings["slm_width"] // 2
    cy = settings["slm_height"] // 2

    used = 0
    skipped_camera = 0
    skipped_slm = 0

    for x_cam, y_cam in points:
        if abs(x_cam) > settings["cam_width"] / 2.0 or abs(y_cam) > settings["cam_height"] / 2.0:
            skipped_camera += 1
            continue

        fft_x = cx + int(round((x_cam * cam_dx) / focal_dx))
        fft_y = cy - int(round((y_cam * cam_dy) / focal_dy))

        if fft_x < 0 or fft_x >= settings["slm_width"] or fft_y < 0 or fft_y >= settings["slm_height"]:
            skipped_slm += 1
            continue

        if target[fft_y, fft_x] == 0:
            used += 1
        target[fft_y, fft_x] = 1.0

    return target, {
        "requested": len(points),
        "used": used,
        "skipped_camera": skipped_camera,
        "skipped_slm": skipped_slm,
    }


def _starting_phase(settings, mode="checkerboard"):
    h = settings["slm_height"]
    w = settings["slm_width"]
    if mode == "random":
        return np.random.uniform(-math.pi, math.pi, size=(h, w))
    y, x = np.indices((h, w))
    if mode == "binary_grating":
        return np.where((x % 2) == 0, 0.0, math.pi)
    return np.where(((x + y) % 2) == 0, 0.0, math.pi)


def phase_to_u8(phase):
    wrapped = np.mod(phase, TWO_PI)
    return np.clip((wrapped / TWO_PI) * 255.0, 0, 255).astype(np.uint8)


def _gs_loop(source, target, settings, weighted=False):
    phase = _starting_phase(settings)
    target_weights = np.ones_like(target, dtype=np.float32)
    active = target > 0
    relaxation = min(max(float(settings.get("wgs_relaxation", 0.5)), 0.0), 1.0)

    for _ in range(max(int(settings.get("iterations", 20)), 1)):
        slm_field = source * np.exp(1j * phase)
        focal = np.fft.fftshift(np.fft.fft2(np.fft.ifftshift(slm_field)))

        focal_phase = np.angle(focal)
        focal_amp = np.abs(focal)

        if weighted and np.any(active):
            mean_amp = float(np.mean(focal_amp[active]))
            if mean_amp > 0:
                ratio = np.ones_like(focal_amp, dtype=np.float32)
                ratio[active] = mean_amp / np.maximum(focal_amp[active], 1e-6)
                target_weights[active] = (1.0 - relaxation) * target_weights[active] + relaxation * ratio[active]

        desired_amp = target * target_weights if weighted else target
        focal_new = desired_amp * np.exp(1j * focal_phase)
        slm_back = np.fft.fftshift(np.fft.ifft2(np.fft.ifftshift(focal_new)))
        phase = np.angle(slm_back)

    return phase_to_u8(phase)


def random_mask_encoding(points, settings):
    target, _ = points_to_target_amplitude(points, settings)
    ys, xs = np.where(target > 0)
    if len(xs) == 0:
        raise ValueError("No valid target points for RME.")

    offsets = list(zip(xs - settings["slm_width"] // 2, settings["slm_height"] // 2 - ys))
    trap_count = len(offsets)

    pixel_count = settings["slm_width"] * settings["slm_height"]
    rng = np.random.default_rng(0xA511E9B3 + trap_count)
    order = np.arange(pixel_count)
    rng.shuffle(order)

    trap_for_pixel = np.zeros(pixel_count, dtype=np.int32)
    base = pixel_count // trap_count
    rem = pixel_count % trap_count
    cursor = 0
    for i in range(trap_count):
        quota = base + (1 if i < rem else 0)
        chosen = order[cursor:cursor + quota]
        trap_for_pixel[chosen] = i
        cursor += quota

    trap_for_pixel = trap_for_pixel.reshape(settings["slm_height"], settings["slm_width"])
    y, x = np.indices((settings["slm_height"], settings["slm_width"]), dtype=float)
    off_x = np.array([p[0] for p in offsets], dtype=float)[trap_for_pixel]
    off_y = np.array([p[1] for p in offsets], dtype=float)[trap_for_pixel]
    phase = TWO_PI * ((off_x * x / settings["slm_width"]) - (off_y * y / settings["slm_height"]))
    return phase_to_u8(phase)


def generate_phase_mask(points, settings, algorithm, source_amplitude=None):
    points = list(points)
    if not points:
        raise ValueError("No target points provided.")

    target, info = points_to_target_amplitude(points, settings)
    if info["used"] == 0:
        raise ValueError("All points were outside valid camera/SLM mapping bounds.")

    source = source_amplitude
    if source is None:
        source = gaussian_source(settings["slm_width"], settings["slm_height"])

    mode = str(algorithm).lower().strip()
    if mode == "rme":
        return random_mask_encoding(points, settings), info
    if mode == "wgs":
        return _gs_loop(source, target, settings, weighted=True), info
    return _gs_loop(source, target, settings, weighted=False), info
