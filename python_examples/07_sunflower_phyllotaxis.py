import math

# Example 07 – Sunflower / Phyllotaxis
# ───────────────────────────────────────
# Uses Vogel's formula to distribute traps like seeds in a sunflower head:
#   r     = c * sqrt(n)
#   theta = n * golden_angle   (golden_angle ≈ 137.5°)
#
# This maximises the packing uniformity across the focal plane.

def build_pattern(width, height):
    n_pts        = 55
    c            = min(width, height) * 0.028   # radial scale factor
    golden_angle = math.pi * (3 - math.sqrt(5)) # ≈ 2.3999 rad ≈ 137.508°

    pts = []
    for n in range(n_pts):
        r     = c * math.sqrt(n)
        theta = n * golden_angle
        pts.append([r * math.cos(theta), r * math.sin(theta)])
    return pts
