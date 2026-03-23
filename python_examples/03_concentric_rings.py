import math

# Example 03 – Concentric Rings
# ──────────────────────────────
# Generates multiple concentric rings of traps.
# Each outer ring has proportionally more traps than the inner one.

def build_pattern(width, height):
    rings       = 3
    base_points = 6            # traps on the innermost ring
    base_radius = min(width, height) * 0.10

    pts = []
    for r in range(1, rings + 1):
        radius = base_radius * r
        n_pts  = base_points * r
        for i in range(n_pts):
            a = 2 * math.pi * i / n_pts
            pts.append([radius * math.cos(a), radius * math.sin(a)])
    return pts
