# Python Trap Scripting Guide

## What This Feature Does
The `Python` tab is static-first:
- easiest path: return one static pattern
- optional path: return multiple frames for motion

The same GS + SLM pipeline is used for both.

## Script Entry Points
You can use either of these:

1. `build_pattern(width, height)` (recommended for static)
2. `build_frames(frame_count, width, height)` (for motion or advanced control)

Precedence:
- If `build_pattern` exists, it is used.
- Otherwise `build_frames` is used.

If a single frame is returned, the app treats it as static and repeats it.

## Script-Declared Settings
You can optionally define these variables at the top of your script to override the UI spinboxes:

```python
FRAME_COUNT = 60   # Overrides the "No. of frames" spinbox
FPS         = 30   # Overrides the "Frame rate" spinbox
```
This is useful for making your animated `.py` files fully self-contained.

## Built-in Pattern Helpers
The app injects helper namespace `hot.pattern`:
- `circle`
- `triangle`
- `square`
- `rectangle`
- `hexagon`
- `two_spots`
- `star`
- `planet_moon`
- `grid`

All helpers return one frame: `[[x, y], [x, y], ...]`.

### Helper Signatures
```python
hot.pattern.circle(point_count=12, radius=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.triangle(point_count=9, scale=None, rotation_deg=0, x_shift=0, y_shift=0, symmetric=True)
hot.pattern.square(point_count=12, size=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.rectangle(point_count=16, width=None, height=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.hexagon(point_count=18, radius=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.two_spots(distance=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.star(point_count=20, star_points=5, outer_radius=None, inner_radius=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.planet_moon(point_count=20, planet_radius=None, moon_radius=None, distance=None, rotation_deg=0, x_shift=0, y_shift=0)
hot.pattern.grid(rows=4, cols=4, row_spacing=None, col_spacing=None, rotation_deg=0, x_shift=0, y_shift=0, center=True)
```

## Coordinate System
Coordinates are centered camera coordinates:
- `(0, 0)` is center
- `+x` right, `+y` up
- bounds are about `[-width/2, +width/2]`, `[-height/2, +height/2]`

Out-of-range points are clamped automatically with warning text.

## Python Modules
You can import normal Python modules in scripts.

- `math`: always available (part of Python standard library)
- `numpy`: bundled by this app build in `Lib/site-packages` for embedded runtime

Example:
```python
import math
import numpy as np
```

## Python Tab Workflow
UI is intentionally minimal:
1. Write code in editor
2. Click `Run Code` (builds sequence)
3. Click `Send` (play/send sequence)

## Examples

### 1. Simple static circle
```python
def build_pattern(width, height):
    return hot.pattern.circle(point_count=16)
```

### 2. Static rectangle
```python
def build_pattern(width, height):
    return hot.pattern.rectangle(point_count=20, width=220, height=120)
```

### 3. Static hexagon + center point
```python
def build_pattern(width, height):
    ring = hot.pattern.hexagon(point_count=18)
    return ring + [[0, 0]]
```

### 4. Square corners + surrounding 6-point circle
```python
def build_pattern(width, height):
    square = hot.pattern.square(point_count=4, size=min(width, height) * 0.35, rotation_deg=45)
    circle = hot.pattern.circle(point_count=6, radius=min(width, height) * 0.30)
    return square + circle
```

### 5. Optional motion (rotating circle)
```python
import math

def build_frames(frame_count, width, height):
    frames = []
    r = min(width, height) * 0.25
    n = 12

    for f in range(frame_count):
        t = 0.0 if frame_count <= 1 else f / float(frame_count - 1)
        rot = 2.0 * math.pi * t
        frame = []
        for i in range(n):
            a = rot + 2.0 * math.pi * i / n
            frame.append([r * math.cos(a), r * math.sin(a)])
        frames.append(frame)

    return frames
```

## Common Errors
- `Script must define callable build_pattern(...) or build_frames(...)`
  - Add one valid entrypoint.
- `Frame N has no trap points`
  - Ensure each returned frame has at least one `[x, y]`.
- `Point M in frame N must be [x, y] numeric coordinates`
  - Return numeric pairs only.

## Notes
- Use `build_pattern` for most static use cases.
- Use `build_frames` only when you want motion.
