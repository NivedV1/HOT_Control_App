# Line sweep animation example for HOT Python tab

def build_frames(frame_count, width, height):
    trap_count = 10
    spacing = min(width, height) * 0.07
    sweep_min = -width * 0.30
    sweep_max = width * 0.30

    frames = []
    for f in range(frame_count):
        t = 0.0 if frame_count <= 1 else f / float(frame_count - 1)
        x_offset = sweep_min + (sweep_max - sweep_min) * t

        frame = []
        y0 = -0.5 * (trap_count - 1) * spacing
        for i in range(trap_count):
            y = y0 + i * spacing
            frame.append([x_offset, y])

        frames.append(frame)

    return frames
