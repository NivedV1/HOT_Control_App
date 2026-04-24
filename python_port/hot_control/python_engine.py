from __future__ import annotations

import math

from .patterns import generate_pattern


class PatternHelpers:
    def circle(self, point_count=12, radius=120.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "circle",
            "point_count": point_count,
            "radius": radius,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def triangle(self, point_count=9, scale=120.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "triangle",
            "point_count": point_count,
            "size": scale,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def square(self, point_count=12, size=140.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "square",
            "point_count": point_count,
            "size": size,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def rectangle(self, point_count=16, width=220.0, height=120.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "rectangle",
            "point_count": point_count,
            "width": width,
            "height": height,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def hexagon(self, point_count=18, radius=120.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "hexagon",
            "point_count": point_count,
            "radius": radius,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def two_spots(self, distance=150.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "two_spots",
            "distance": distance,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def star(self, point_count=20, star_points=5, outer_radius=140.0, inner_radius=70.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "star",
            "point_count": point_count,
            "star_points": star_points,
            "radius": outer_radius,
            "inner_radius": inner_radius,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def planet_moon(self, point_count=20, planet_radius=120.0, moon_radius=40.0, distance=180.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "planet_moon",
            "point_count": point_count,
            "radius": planet_radius,
            "moon_radius": moon_radius,
            "distance": distance,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })

    def grid(self, rows=4, cols=4, row_spacing=40.0, col_spacing=40.0, rotation_deg=0.0, x_shift=0.0, y_shift=0.0):
        return generate_pattern({
            "preset": "grid",
            "grid_rows": rows,
            "grid_cols": cols,
            "row_spacing": row_spacing,
            "col_spacing": col_spacing,
            "rotation_deg": rotation_deg,
            "x_shift": x_shift,
            "y_shift": y_shift,
        })


class HotNamespace:
    def __init__(self):
        self.pattern = PatternHelpers()


def _coerce_frames(raw):
    if raw is None:
        raise ValueError("Script returned no data.")

    def pair(value):
        if not isinstance(value, (list, tuple)) or len(value) != 2:
            raise ValueError("Each point must be [x, y].")
        return float(value[0]), float(value[1])

    if isinstance(raw, (list, tuple)) and raw and isinstance(raw[0], (list, tuple)) and len(raw[0]) == 2 and isinstance(raw[0][0], (int, float)):
        return [[pair(p) for p in raw]]

    frames = []
    for frame in raw:
        frame_points = [pair(p) for p in frame]
        if not frame_points:
            raise ValueError("Each frame must contain at least one point.")
        frames.append(frame_points)

    if not frames:
        raise ValueError("No frames were produced.")
    return frames


def run_python_trap_script(code, frame_count, width, height):
    scope = {
        "math": math,
        "hot": HotNamespace(),
    }
    exec(code, scope)

    fps = int(scope.get("FPS", 30))
    script_frame_count = int(scope.get("FRAME_COUNT", frame_count))

    if callable(scope.get("build_pattern")):
        raw = scope["build_pattern"](width, height)
        frames = _coerce_frames(raw)
    elif callable(scope.get("build_frames")):
        raw = scope["build_frames"](script_frame_count, width, height)
        frames = _coerce_frames(raw)
    else:
        raise ValueError("Script must define build_pattern(width, height) or build_frames(frame_count, width, height).")

    if len(frames) == 1 and script_frame_count > 1:
        frames = frames * script_frame_count

    return {
        "frames": frames,
        "fps": max(1, fps),
        "frame_count": len(frames),
    }
