"""
Simple standalone Gerchberg-Saxton phase mask code.

This is written as plain Python reference code, not as app/UI code.
It uses the same important variables as the HOT app:

- SLM size and pixel size
- camera size and pixel size
- camera imaging magnification
- laser wavelength
- Fourier lens focal length
- iteration count
- source amplitude
- grid-click point targets

Dependencies:
    pip install numpy pillow
"""

from pathlib import Path

import numpy as np
from PIL import Image


TWO_PI = 2.0 * np.pi


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

        # GS settings
        "iterations": 20,
        # Options: "checkerboard", "binary_grating", "random"
        "starting_phase": "checkerboard",

        # If this is None, a default Gaussian source is used.
        # If provided, shape must be (slm_height, slm_width).
        "source_amplitude": None,
        "default_source_beam_waist_px": None,
    }


def gaussian_source(width, height, beam_waist_px):
    """Default SLM source amplitude, same idea as the app's Gaussian source."""
    y, x = np.indices((height, width), dtype=float)
    cx = (width - 1) / 2.0
    cy = (height - 1) / 2.0
    sigma = max(float(beam_waist_px), 1e-6)
    r2 = (x - cx) ** 2 + (y - cy) ** 2
    return np.exp(-r2 / (2.0 * sigma * sigma)).astype(np.float32)


def get_source_amplitude(settings):
    """Use a custom source if supplied, otherwise build the default Gaussian."""
    width = settings["slm_width"]
    height = settings["slm_height"]
    source = settings.get("source_amplitude")

    if source is not None:
        source = np.asarray(source, dtype=np.float32)
        if source.shape == (height, width):
            return source
        if source.size == width * height:
            return source.reshape(height, width)
        raise ValueError("source_amplitude has the wrong size")

    waist = settings.get("default_source_beam_waist_px")
    if waist is None or waist <= 0:
        waist = min(width, height) / 6.0
    return gaussian_source(width, height, waist)


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


def point_targets_to_amplitude(points, settings):
    """
    Convert camera-centered trap points to an SLM-sized focal-plane target amplitude.

    Point coordinates are camera-centered:
    (0, 0) is center, +x is right, +y is up.
    """
    width = settings["slm_width"]
    height = settings["slm_height"]
    cam_width = settings["cam_width"]
    cam_height = settings["cam_height"]

    cam_dx, cam_dy, focal_dx, focal_dy = mapping_values(settings)
    target = np.zeros((height, width), dtype=np.float32)
    cx = width // 2
    cy = height // 2

    used = 0
    skipped_camera = 0
    skipped_slm = 0

    for x_cam, y_cam in points:
        if abs(x_cam) > cam_width / 2.0 or abs(y_cam) > cam_height / 2.0:
            skipped_camera += 1
            continue

        fft_x = cx + int(np.rint((x_cam * cam_dx) / focal_dx))
        fft_y = cy - int(np.rint((y_cam * cam_dy) / focal_dy))

        if fft_x < 0 or fft_x >= width or fft_y < 0 or fft_y >= height:
            skipped_slm += 1
            continue

        if target[fft_y, fft_x] == 0:
            used += 1
        target[fft_y, fft_x] = 1.0

    info = {
        "requested": len(points),
        "used": used,
        "skipped_camera": skipped_camera,
        "skipped_slm": skipped_slm,
    }
    return target, info


def make_starting_phase(settings):
    """Initial phase guess used by GS."""
    width = settings["slm_width"]
    height = settings["slm_height"]
    mode = settings.get("starting_phase", "checkerboard")

    if mode == "random":
        return np.random.uniform(-np.pi, np.pi, size=(height, width))

    y, x = np.indices((height, width))
    if mode == "binary_grating":
        return np.where((x % 2) == 0, 0.0, np.pi)

    return np.where(((x + y) % 2) == 0, 0.0, np.pi)


def phase_to_8bit(phase):
    """Wrap phase to [0, 2pi) and convert it to 8-bit grayscale."""
    wrapped = np.mod(phase, TWO_PI)
    mask = np.clip((wrapped / TWO_PI) * 255.0, 0, 255).astype(np.uint8)
    return mask, wrapped


def gerchberg_saxton(source_amplitude, target_amplitude, settings):
    """
    Main GS loop.

    This is the core algorithm:
    1. SLM field = source amplitude * exp(i * SLM phase)
    2. Fourier transform to focal plane
    3. Keep focal phase, replace focal amplitude with target amplitude
    4. Inverse Fourier transform back to SLM plane
    5. Keep only the new SLM phase
    """
    phase = make_starting_phase(settings)

    for _ in range(settings["iterations"]):
        slm_field = source_amplitude * np.exp(1j * phase)

        focal_field = np.fft.fftshift(
            np.fft.fft2(np.fft.ifftshift(slm_field))
        )

        focal_field = target_amplitude * np.exp(1j * np.angle(focal_field))

        slm_field = np.fft.fftshift(
            np.fft.ifft2(np.fft.ifftshift(focal_field))
        )

        phase = np.angle(slm_field)

    return phase_to_8bit(phase)


def generate_from_points(points, settings=None):
    """Generate phase mask from camera-centered trap points."""
    if settings is None:
        settings = default_settings()

    source = get_source_amplitude(settings)
    target, info = point_targets_to_amplitude(points, settings)
    if info["used"] == 0:
        raise ValueError("no valid target points after camera/Fourier mapping")

    mask, wrapped_phase = gerchberg_saxton(source, target, settings)
    return {
        "phase_mask_8bit": mask,
        "wrapped_phase_rad": wrapped_phase,
        "info": info,
    }


def save_mask(path, phase_mask_8bit):
    """Save an 8-bit grayscale phase mask."""
    Image.fromarray(np.asarray(phase_mask_8bit, dtype=np.uint8), mode="L").save(Path(path))


if __name__ == "__main__":
    # Small example using grid-click style point coordinates.
    settings = default_settings()
    settings["iterations"] = 20

    result = generate_from_points([(0.0, 0.0), (100.0, 0.0)], settings)
    save_mask("gs_points_mask.png", result["phase_mask_8bit"])
