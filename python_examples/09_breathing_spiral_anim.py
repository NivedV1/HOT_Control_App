import math

# Example 09 – Breathing Spiral  [ANIMATED]
# ──────────────────────────────────────────
# An Archimedean spiral whose radial scale pulses in and out
# like a breathing motion, driven by a sine envelope.
#
# FRAME_COUNT and FPS declared here override the UI spinboxes.

FRAME_COUNT = 90   # use a multiple of the breathing period for clean loops
FPS         = 30

def build_frames(frame_count, width, height):
    n_pts   = 30
    n_turns = 2.5
    max_a   = min(width, height) * 0.045   # maximum radial scale

    frames = []
    for f in range(frame_count):
        phase = math.sin(2 * math.pi * f / frame_count)   # oscillates -1 .. 1
        a     = max_a * (0.5 + 0.5 * phase)               # ranges 0 .. max_a

        frame = []
        for i in range(n_pts):
            theta = n_turns * 2 * math.pi * i / (n_pts - 1)
            r     = a * theta
            frame.append([r * math.cos(theta), r * math.sin(theta)])
        frames.append(frame)
    return frames
