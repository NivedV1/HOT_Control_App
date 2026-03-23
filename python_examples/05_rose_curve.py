import math

# Example 05 – Rose / Rhodonea Curve
# ────────────────────────────────────
# Polar equation: r = cos(k * theta)
#   k odd  →  k petals
#   k even →  2k petals
#
# Try k = 3, 4, 5, 7 for different flower-like patterns.

def build_pattern(width, height):
    k     = 5                              # petal parameter
    n_pts = 60
    scale = min(width, height) * 0.30

    pts = []
    for i in range(n_pts):
        theta = 2 * math.pi * i / n_pts
        r     = scale * math.cos(k * theta)
        pts.append([r * math.cos(theta), r * math.sin(theta)])
    return pts
