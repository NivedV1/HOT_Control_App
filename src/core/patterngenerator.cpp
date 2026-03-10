#include "patterngenerator.h"

#include <QtMath>

namespace {

constexpr double kTau = 6.28318530717958647692;

QPointF rotatePoint(const QPointF &point, double angleRad) {
    const double c = qCos(angleRad);
    const double s = qSin(angleRad);
    return QPointF(point.x() * c - point.y() * s,
                   point.x() * s + point.y() * c);
}

QVector<QPointF> samplePolygonEdges(const QVector<QPointF> &vertices, int pointCount) {
    QVector<QPointF> points;
    if (vertices.size() < 2 || pointCount <= 0) {
        return points;
    }

    QVector<double> edgeLengths;
    edgeLengths.reserve(vertices.size());

    double perimeter = 0.0;
    for (int i = 0; i < vertices.size(); ++i) {
        const QPointF &a = vertices[i];
        const QPointF &b = vertices[(i + 1) % vertices.size()];
        const double length = qSqrt(qPow(b.x() - a.x(), 2) + qPow(b.y() - a.y(), 2));
        edgeLengths.append(length);
        perimeter += length;
    }

    if (perimeter <= 0.0) {
        return points;
    }

    points.reserve(pointCount);

    for (int i = 0; i < pointCount; ++i) {
        const double targetDistance = (static_cast<double>(i) / static_cast<double>(pointCount)) * perimeter;

        double accumulated = 0.0;
        for (int edgeIndex = 0; edgeIndex < vertices.size(); ++edgeIndex) {
            const double edgeLength = edgeLengths[edgeIndex];
            if (edgeLength <= 0.0) {
                continue;
            }

            if (targetDistance <= accumulated + edgeLength || edgeIndex == vertices.size() - 1) {
                const QPointF &start = vertices[edgeIndex];
                const QPointF &end = vertices[(edgeIndex + 1) % vertices.size()];
                const double localT = qBound(0.0, (targetDistance - accumulated) / edgeLength, 1.0);
                points.append(QPointF(start.x() + (end.x() - start.x()) * localT,
                                      start.y() + (end.y() - start.y()) * localT));
                break;
            }

            accumulated += edgeLength;
        }
    }

    return points;
}

QVector<QPointF> makeRegularPolygon(int sides, double radius, double rotationDeg) {
    QVector<QPointF> vertices;
    if (sides < 3 || radius <= 0.0) {
        return vertices;
    }

    vertices.reserve(sides);

    const double baseRotationRad = qDegreesToRadians(rotationDeg);
    for (int i = 0; i < sides; ++i) {
        const double angle = (kTau * static_cast<double>(i) / static_cast<double>(sides)) + baseRotationRad;
        vertices.append(QPointF(radius * qCos(angle), radius * qSin(angle)));
    }

    return vertices;
}

QVector<QPointF> makeRectangleVertices(double width, double height, double rotationDeg) {
    QVector<QPointF> vertices;
    if (width <= 0.0 || height <= 0.0) {
        return vertices;
    }

    const double halfW = width / 2.0;
    const double halfH = height / 2.0;
    const double angleRad = qDegreesToRadians(rotationDeg);

    const QVector<QPointF> base = {
        QPointF(-halfW, -halfH),
        QPointF(halfW, -halfH),
        QPointF(halfW, halfH),
        QPointF(-halfW, halfH)
    };

    vertices.reserve(base.size());
    for (const QPointF &p : base) {
        vertices.append(rotatePoint(p, angleRad));
    }

    return vertices;
}

QVector<QPointF> applyShift(const QVector<QPointF> &points, double xShift, double yShift) {
    QVector<QPointF> shifted;
    shifted.reserve(points.size());
    for (const QPointF &p : points) {
        shifted.append(QPointF(p.x() + xShift, p.y() + yShift));
    }
    return shifted;
}

} // namespace

namespace PatternGenerator {

QVector<QPointF> generate(const PatternRequest &request) {
    QVector<QPointF> points;

    const int count = qMax(1, request.pointCount);
    const double shiftX = (request.preset == Preset::Triangle && request.symmetric) ? 0.0 : request.xShift;
    const double shiftY = (request.preset == Preset::Triangle && request.symmetric) ? 0.0 : request.yShift;

    switch (request.preset) {
    case Preset::Circle: {
        const double radius = qMax(0.0, request.radius);
        if (radius <= 0.0) {
            return points;
        }

        const double angleOffset = qDegreesToRadians(request.rotationDeg);
        points.reserve(count);
        for (int i = 0; i < count; ++i) {
            const double angle = (kTau * static_cast<double>(i) / static_cast<double>(count)) + angleOffset;
            points.append(QPointF(radius * qCos(angle) + shiftX,
                                  radius * qSin(angle) + shiftY));
        }
        return points;
    }

    case Preset::Triangle: {
        const double scale = qMax(0.0, request.scale);
        if (scale <= 0.0) {
            return points;
        }

        QVector<QPointF> vertices;
        vertices.reserve(3);
        const double angleOffset = qDegreesToRadians(request.rotationDeg - 90.0);
        for (int i = 0; i < 3; ++i) {
            const double angle = angleOffset + (kTau * static_cast<double>(i) / 3.0);
            vertices.append(QPointF(scale * qCos(angle), scale * qSin(angle)));
        }

        points = samplePolygonEdges(vertices, count);
        return applyShift(points, shiftX, shiftY);
    }

    case Preset::Square: {
        const QVector<QPointF> vertices = makeRectangleVertices(request.size, request.size, request.rotationDeg);
        points = samplePolygonEdges(vertices, count);
        return applyShift(points, shiftX, shiftY);
    }

    case Preset::Rectangle: {
        const QVector<QPointF> vertices = makeRectangleVertices(request.width, request.height, request.rotationDeg);
        points = samplePolygonEdges(vertices, count);
        return applyShift(points, shiftX, shiftY);
    }

    case Preset::Hexagon: {
        const QVector<QPointF> vertices = makeRegularPolygon(6, request.radius, request.rotationDeg);
        points = samplePolygonEdges(vertices, count);
        return applyShift(points, shiftX, shiftY);
    }

    case Preset::TwoSpots: {
        const double dist = qMax(0.0, request.distance);
        const double angleOffset = qDegreesToRadians(request.rotationDeg);
        
        points.reserve(2);
        points.append(QPointF(dist * qCos(angleOffset) + shiftX,
                              dist * qSin(angleOffset) + shiftY));
        points.append(QPointF(dist * qCos(angleOffset + (kTau / 2.0)) + shiftX,
                              dist * qSin(angleOffset + (kTau / 2.0)) + shiftY));
        return points;
    }
    }

    return points;
}

} // namespace PatternGenerator

