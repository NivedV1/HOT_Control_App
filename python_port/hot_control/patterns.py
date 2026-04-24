from __future__ import annotations

import math


def sample_polygon_edges(vertices, count):
    if len(vertices) < 2 or count <= 0:
        return []

    edges = []
    perimeter = 0.0
    n = len(vertices)
    for i in range(n):
        ax, ay = vertices[i]
        bx, by = vertices[(i + 1) % n]
        length = math.hypot(bx - ax, by - ay)
        edges.append(length)
        perimeter += length

    if perimeter <= 0:
        return []

    out = []
    for i in range(count):
        t = (i / count) * perimeter
        acc = 0.0
        for edge_idx in range(n):
            e = edges[edge_idx]
            ax, ay = vertices[edge_idx]
            bx, by = vertices[(edge_idx + 1) % n]
            if t <= acc + e or edge_idx == n - 1:
                lt = 0.0 if e <= 0 else (t - acc) / e
                lt = min(max(lt, 0.0), 1.0)
                out.append((ax + (bx - ax) * lt, ay + (by - ay) * lt))
                break
            acc += e
    return out


def rotate_point(x, y, deg):
    a = math.radians(deg)
    c = math.cos(a)
    s = math.sin(a)
    return x * c - y * s, x * s + y * c


def shift_points(points, dx, dy):
    return [(x + dx, y + dy) for x, y in points]


def generate_pattern(req):
    c = max(1, int(req.get("point_count", 12)))
    p = str(req.get("preset", "circle")).lower().strip()

    radius = float(req.get("radius", 120.0))
    size = float(req.get("size", 140.0))
    width = float(req.get("width", 220.0))
    height = float(req.get("height", 120.0))
    rot = float(req.get("rotation_deg", 0.0))
    x_shift = float(req.get("x_shift", 0.0))
    y_shift = float(req.get("y_shift", 0.0))

    if p == "circle":
        pts = []
        for i in range(c):
            a = 2 * math.pi * i / c + math.radians(rot)
            pts.append((radius * math.cos(a), radius * math.sin(a)))
        return shift_points(pts, x_shift, y_shift)

    if p == "triangle":
        verts = []
        for i in range(3):
            a = math.radians(rot - 90.0) + 2 * math.pi * i / 3.0
            verts.append((size * math.cos(a), size * math.sin(a)))
        return shift_points(sample_polygon_edges(verts, c), x_shift, y_shift)

    if p == "square":
        h = size / 2.0
        base = [(-h, -h), (h, -h), (h, h), (-h, h)]
        verts = [rotate_point(x, y, rot) for x, y in base]
        return shift_points(sample_polygon_edges(verts, c), x_shift, y_shift)

    if p == "rectangle":
        hw = width / 2.0
        hh = height / 2.0
        base = [(-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)]
        verts = [rotate_point(x, y, rot) for x, y in base]
        return shift_points(sample_polygon_edges(verts, c), x_shift, y_shift)

    if p == "hexagon":
        verts = []
        for i in range(6):
            a = 2 * math.pi * i / 6 + math.radians(rot)
            verts.append((radius * math.cos(a), radius * math.sin(a)))
        return shift_points(sample_polygon_edges(verts, c), x_shift, y_shift)

    if p == "two_spots":
        dist = float(req.get("distance", 150.0))
        a = math.radians(rot)
        return [
            (dist * math.cos(a) + x_shift, dist * math.sin(a) + y_shift),
            (-dist * math.cos(a) + x_shift, -dist * math.sin(a) + y_shift),
        ]

    if p == "star":
        star_points = max(3, int(req.get("star_points", 5)))
        inner = float(req.get("inner_radius", 60.0))
        verts = []
        for i in range(star_points * 2):
            a = math.radians(rot) + math.pi * i / star_points
            r = radius if i % 2 == 0 else inner
            verts.append((r * math.cos(a), r * math.sin(a)))
        return shift_points(sample_polygon_edges(verts, c), x_shift, y_shift)

    if p == "planet_moon":
        moon_radius = float(req.get("moon_radius", 30.0))
        dist = float(req.get("distance", 150.0))
        planet_count = max(1, c // 2)
        moon_count = max(1, c - planet_count)
        pts = []
        for i in range(planet_count):
            a = 2 * math.pi * i / planet_count
            pts.append((radius * math.cos(a), radius * math.sin(a)))
        cx, cy = rotate_point(dist, 0.0, rot)
        for i in range(moon_count):
            a = 2 * math.pi * i / moon_count
            pts.append((cx + moon_radius * math.cos(a), cy + moon_radius * math.sin(a)))
        return shift_points(pts, x_shift, y_shift)

    if p == "grid":
        rows = max(1, int(req.get("grid_rows", 4)))
        cols = max(1, int(req.get("grid_cols", 4)))
        row_spacing = float(req.get("row_spacing", 40.0))
        col_spacing = float(req.get("col_spacing", 40.0))
        w = (cols - 1) * col_spacing
        h = (rows - 1) * row_spacing
        sx = -w / 2.0
        sy = -h / 2.0
        pts = []
        for r in range(rows):
            for cidx in range(cols):
                x = sx + cidx * col_spacing
                y = sy + r * row_spacing
                xr, yr = rotate_point(x, y, rot)
                pts.append((xr + x_shift, yr + y_shift))
        return pts

    return []
