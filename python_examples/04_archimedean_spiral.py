import math

# Example 04 – Archimedean Spiral
# ────────────────────────────────
# Points equally spaced in angle along the curve r = a * theta.
# Increase n_turns or n_pts to get a denser / longer spiral.

def build_pattern(width, height):
    n_turns = 3
    n_pts   = 40
    a       = min(width, height) * 0.04   # spacing between successive turns

    pts = []
    for i in range(n_pts):
        theta = n_turns * 2 * math.pi * i / (n_pts - 1)
        r     = a * theta
        pts.append([r * math.cos(theta), r * math.sin(theta)])
    return pts
