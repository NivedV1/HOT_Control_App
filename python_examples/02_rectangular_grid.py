import math

# Example 02 – Rectangular Grid
# ──────────────────────────────
# Places traps on a uniform cols × rows grid centred in the FOV.
# Spacing is calculated automatically from the camera dimensions.

def build_pattern(width, height):
    cols, rows = 5, 4

    xs = [width  * (c / (cols + 1) - 0.5) for c in range(1, cols + 1)]
    ys = [height * (r / (rows + 1) - 0.5) for r in range(1, rows + 1)]

    return [[x, y] for y in ys for x in xs]
