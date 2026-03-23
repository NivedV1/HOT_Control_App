# Optional motion example (multi-frame animation)
import math

def build_frames(frame_count, width, height):
    frames = []
    trap_count = 12
    radius = min(width, height) * 0.25

    for f in range(frame_count):
        t = 0.0 if frame_count <= 1 else f / float(frame_count - 1)
        rot = 2.0 * math.pi * t
        frame = []
        for i in range(trap_count):
            a = rot + 2.0 * math.pi * i / trap_count
            frame.append([radius * math.cos(a), radius * math.sin(a)])
        frames.append(frame)

    return frames
