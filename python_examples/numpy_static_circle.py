import numpy as np

def build_pattern(width, height):
    n = 12
    r = min(width, height) * 0.25
    angles = np.linspace(0.0, 2.0 * np.pi, n, endpoint=False)
    xs = r * np.cos(angles)
    ys = r * np.sin(angles)
    return [[float(x), float(y)] for x, y in zip(xs, ys)]
