#ifndef PATTERNGENERATOR_H
#define PATTERNGENERATOR_H

#include <QPointF>
#include <QVector>

namespace PatternGenerator {

enum class Preset {
    Circle,
    Triangle,
    Square,
    Rectangle,
    Hexagon,
    TwoSpots,
    Star,
    PlanetAndMoon,
    Grid
};

struct PatternRequest {
    Preset preset = Preset::Circle;
    int pointCount = 12;
    double rotationDeg = 0.0;
    double xShift = 0.0;
    double yShift = 0.0;
    bool symmetric = true;
    double radius = 100.0;
    double scale = 100.0;
    double size = 100.0;
    double width = 120.0;
    double height = 80.0;
    double distance = 50.0;
    
    // Star specific
    double innerRadius = 50.0;
    int starPoints = 5;
    
    // Planet and Moon specific
    double moonRadius = 20.0;
    
    // Grid specific
    int gridRows = 5;
    int gridCols = 5;
    double gridRowSpacing = 20.0;
    double gridColSpacing = 20.0;
    bool centerGridAtOrigin = true;
};

QVector<QPointF> generate(const PatternRequest &request);

} // namespace PatternGenerator

#endif // PATTERNGENERATOR_H