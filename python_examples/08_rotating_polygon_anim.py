import math

# Example 08 – Rotating Polygon  [ANIMATED]
# ──────────────────────────────────────────
# A regular N-gon that completes exactly one full rotation over all frames.
#
# FRAME_COUNT and FPS declared here override the UI spinboxes — this script
# is fully self-contained.

FRAME_COUNT = 60   # total number of frames in the animation
FPS         = 30   # playback speed (frames per second)

def build_frames(frame_count, width, height):
    N      = 6                             # hexagon
    radius = min(width, height) * 0.25

    frames = []
    for f in range(frame_count):
        rot   = 2 * math.pi * f / frame_count
        frame = []
        for i in range(N):
            a = rot + 2 * math.pi * i / N
            frame.append([radius * math.cos(a), radius * math.sin(a)])
        frames.append(frame)
    return frames
