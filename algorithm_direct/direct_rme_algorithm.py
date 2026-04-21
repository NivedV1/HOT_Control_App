"""
Simple standalone Random Mask Encoding phase mask code.

This is plain Python reference code with no UI dependency.

Dependencies:
    pip install numpy pillow
"""

from pathlib import Path

import numpy as np
from PIL import Image


TWO_PI = 2.0 * np.pi
SEED_BASE = 0x2C9277B5E57A4A83


def default_settings():
    """All needed variables in one simple dictionary."""
    return {
        # SLM hardware
        "slm_width": 1920,
        "slm_height": 1080,
        "slm_pixel_size_um": 8.0,

        # Camera / trap plane
        "cam_width": 1920,
        "cam_height": 1080,
        "cam_pixel_size_um": 5.0,
        "camera_imaging_magnification": 1.0,

        # Optical setup
        "wavelength_nm": 1064.0,
        "focal_length_mm": 100.0,
    }


def mapping_values(settings):
    """Compute physical camera pixel pitch and focal-plane FFT-grid pitch."""
    slm_dx = settings["slm_pixel_size_um"] * 1e-6
    slm_dy = settings["slm_pixel_size_um"] * 1e-6

    effective_cam_pixel_um = (
        settings["cam_pixel_size_um"] / settings["camera_imaging_magnification"]
    )
    cam_dx = effective_cam_pixel_um * 1e-6
    cam_dy = effective_cam_pixel_um * 1e-6

    wavelength = settings["wavelength_nm"] * 1e-9
    focal_length = settings["focal_length_mm"] * 1e-3

    focal_dx = wavelength * focal_length / (settings["slm_width"] * slm_dx)
    focal_dy = wavelength * focal_length / (settings["slm_height"] * slm_dy)
    return cam_dx, cam_dy, focal_dx, focal_dy


def map_points_to_fft_offsets(points, settings):
    """
    Convert camera-centered trap points to Fourier-grid offsets.

    Point coordinates are camera-centered:
    (0, 0) is center, +x is right, +y is up.
    """
    width = settings["slm_width"]
    height = settings["slm_height"]
    cam_width = settings["cam_width"]
    cam_height = settings["cam_height"]

    cam_dx, cam_dy, focal_dx, focal_dy = mapping_values(settings)
    cx = width // 2
    cy = height // 2

    offsets = set()
    skipped_camera = 0
    skipped_slm = 0

    for x_cam, y_cam in points:
        if abs(x_cam) > cam_width / 2.0 or abs(y_cam) > cam_height / 2.0:
            skipped_camera += 1
            continue

        fft_offset_x = int(np.rint((x_cam * cam_dx) / focal_dx))
        fft_offset_y = int(np.rint((y_cam * cam_dy) / focal_dy))

        fft_x = cx + fft_offset_x
        fft_y = cy - fft_offset_y

        if fft_x < 0 or fft_x >= width or fft_y < 0 or fft_y >= height:
            skipped_slm += 1
            continue

        offsets.add((fft_offset_x, fft_offset_y))

    info = {
        "requested": len(points),
        "used": len(offsets),
        "skipped_camera": skipped_camera,
        "skipped_slm": skipped_slm,
    }
    return sorted(offsets), info


def seed_mix(seed, value):
    """Small deterministic seed mixer, matching the C++ idea."""
    seed ^= value + 0x9E3779B97F4A7C15 + ((seed << 6) & 0xFFFFFFFFFFFFFFFF) + (seed >> 2)
    return seed & 0xFFFFFFFFFFFFFFFF


def deterministic_seed(width, height, offsets):
    """Build a deterministic random seed from the target geometry."""
    seed = SEED_BASE
    seed = seed_mix(seed, width)
    seed = seed_mix(seed, height)
    seed = seed_mix(seed, len(offsets))

    for ox, oy in offsets:
        seed = seed_mix(seed, int(ox) & 0xFFFFFFFFFFFFFFFF)
        seed = seed_mix(seed, int(oy) & 0xFFFFFFFFFFFFFFFF)

    out = ((seed >> 32) ^ (seed & 0xFFFFFFFF)) & 0xFFFFFFFF
    return out if out != 0 else 0xA511E9B3


def phase_to_8bit(phase):
    """Wrap phase to [0, 2pi) and convert it to 8-bit grayscale."""
    wrapped = np.mod(phase, TWO_PI)
    mask = np.clip((wrapped / TWO_PI) * 255.0, 0, 255).astype(np.uint8)
    return mask, wrapped


def random_mask_encoding(points, settings=None):
    """
    Generate a Random Mask Encoding phase mask.

    Idea:
    1. Map every target point to a Fourier-grid offset.
    2. Randomly assign each SLM pixel to one target.
    3. For each SLM pixel, write the grating phase for its assigned target.
    """
    if settings is None:
        settings = default_settings()

    width = settings["slm_width"]
    height = settings["slm_height"]
    pixel_count = width * height

    offsets, info = map_points_to_fft_offsets(points, settings)
    if not offsets:
        raise ValueError("no valid target points after camera/Fourier mapping")

    trap_count = len(offsets)
    rng = np.random.default_rng(deterministic_seed(width, height, offsets))

    # Shuffle all SLM pixels and split them as evenly as possible across traps.
    pixel_order = np.arange(pixel_count)
    rng.shuffle(pixel_order)

    trap_for_pixel = np.zeros(pixel_count, dtype=int)
    cursor = 0
    base_count = pixel_count // trap_count
    remainder = pixel_count % trap_count

    for trap_index in range(trap_count):
        quota = base_count + (1 if trap_index < remainder else 0)
        chosen_pixels = pixel_order[cursor: cursor + quota]
        trap_for_pixel[chosen_pixels] = trap_index
        cursor += quota

    trap_for_pixel = trap_for_pixel.reshape(height, width)

    y, x = np.indices((height, width), dtype=float)
    offset_x = np.array([p[0] for p in offsets], dtype=float)[trap_for_pixel]
    offset_y = np.array([p[1] for p in offsets], dtype=float)[trap_for_pixel]

    phase = TWO_PI * ((offset_x * x / width) - (offset_y * y / height))
    mask, wrapped_phase = phase_to_8bit(phase)

    return {
        "phase_mask_8bit": mask,
        "wrapped_phase_rad": wrapped_phase,
        "info": info,
    }


def save_mask(path, phase_mask_8bit):
    """Save an 8-bit grayscale phase mask."""
    Image.fromarray(np.asarray(phase_mask_8bit, dtype=np.uint8), mode="L").save(Path(path))


if __name__ == "__main__":
    settings = default_settings()
    result = random_mask_encoding([(0.0, 0.0), (100.0, 0.0)], settings)
    save_mask("rme_mask.png", result["phase_mask_8bit"])
