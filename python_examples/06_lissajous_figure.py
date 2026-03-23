import math

# Example 06 – Lissajous Figure
# ──────────────────────────────
# Parametric equations:
#   x = A * sin(a*t + delta)
#   y = B * sin(b*t)
#
# The ratio a:b and the phase delta determine the knot shape.
# Classic ratios: 1:2, 2:3, 3:4.

def build_pattern(width, height):
    a     = 3               # x-frequency
    b     = 2               # y-frequency
    delta = math.pi / 4     # phase offset (radians)
    n_pts = 50
    A     = width  * 0.30
    B     = height * 0.30

    pts = []
    for i in range(n_pts):
        t = 2 * math.pi * i / n_pts
        pts.append([A * math.sin(a * t + delta),
                    B * math.sin(b * t)])
    return pts
