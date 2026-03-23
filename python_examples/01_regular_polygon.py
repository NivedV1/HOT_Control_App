import math

# Example 01 – Regular Polygon
# ─────────────────────────────
# Generates N evenly-spaced traps arranged as a regular N-gon
# inscribed in a circle of given radius.
#
# Origin is the camera centre; units are pixels.

def build_pattern(width, height):
    N      = 8                           # number of vertices
    radius = min(width, height) * 0.25

    pts = []
    for i in range(N):
        a = 2 * math.pi * i / N - math.pi / 2   # start at top
        pts.append([radius * math.cos(a), radius * math.sin(a)])
    return pts
