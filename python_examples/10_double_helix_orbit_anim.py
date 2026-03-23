import math

# Example 10 – Double Helix Orbit  [ANIMATED]
# ─────────────────────────────────────────────
# Two counter-wound strands of traps orbit continuously,
# projecting a 3-D helical structure onto the XY focal plane.
# Strand A and Strand B are offset by pi (half a turn) from each other.

def build_frames(frame_count, width, height):
    n_per_strand = 8
    radius       = min(width, height) * 0.22
    z_scale      = min(width, height) * 0.25   # vertical amplitude

    frames = []
    for f in range(frame_count):
        t     = 2 * math.pi * f / frame_count
        frame = []
        for k in range(n_per_strand):
            phi = 2 * math.pi * k / n_per_strand

            # Strand A
            ax = radius * math.cos(t + phi)
            ay = z_scale * math.sin(t + phi)
            frame.append([ax, ay])

            # Strand B – offset by half a turn (pi) → opposite side
            bx = radius * math.cos(t + phi + math.pi)
            by = z_scale * math.sin(t + phi + math.pi)
            frame.append([bx, by])

        frames.append(frame)
    return frames
