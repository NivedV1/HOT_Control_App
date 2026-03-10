#ifndef TARGETGRIDWIDGET_H
#define TARGETGRIDWIDGET_H

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QVector>
#include <QPointF>
#include <QImage>

class QGraphicsPixmapItem;

class GridPoint : public QGraphicsItem {
public:
    GridPoint(QPointF pixelCoords, int pointId, QGraphicsItem *parent = nullptr);
    
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    
    QPointF getPixelCoordinates() const { return pixelCoordinates; }
    void setPixelCoordinates(QPointF coords) { pixelCoordinates = coords; }
    int getPointId() const { return pointId; }
    bool isSelected() const { return selected; }
    void setSelected(bool sel) { selected = sel; update(); }
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    
private:
    QPointF pixelCoordinates;  // Coordinates in pixels
    int pointId;
    bool selected;
    bool isDragging;
    static const double POINT_RADIUS;
};

class TargetGridWidget : public QGraphicsView {
    Q_OBJECT

public:
    explicit TargetGridWidget(int cameraWidth = 1920, int cameraHeight = 1080, QWidget *parent = nullptr);
    ~TargetGridWidget();
    
    // Add/remove points
    void addPoint(QPointF pixelCoords);
    void removePoint(int pointId);
    void clearAllPoints();
    
    // Get all points
    QVector<QPair<int, QPointF>> getAllPoints() const;
    
    // Grid settings
    void setGridResolution(int cameraWidth, int cameraHeight);
    
    // Center the view
    void centerView();
    
    // Theme support
    void setDarkMode(bool dark);
    
    // Keyboard support for moving selected point
    void moveSelectedPoint(int deltaX, int deltaY);  // Delta in pixels

    // Image mode support
    void setBackgroundImage(const QImage &imgGrayCameraSized);
    void clearBackgroundImage();
    void setImageMode(bool enabled);
    bool isImageMode() const { return imageMode; }
    
signals:
    void pointAdded(int pointId, QPointF pixelCoords);
    void pointMoved(int pointId, QPointF newPixelCoords);
    void pointRemoved(int pointId);
    void pointSelected(int pointId);
    void pointDeselected();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void updateGridDisplay();
    void deselectAllPoints();
    bool removeLastCreatedPoint();
    QPointF screenToPixel(QPointF screenPos) const;
    QPointF pixelToScreen(QPointF pixelPos) const;
    void fitGridToView();
    
    QGraphicsScene *gridScene;
    int cameraWidth;      // Camera resolution width in pixels
    int cameraHeight;     // Camera resolution height in pixels
    QVector<GridPoint*> gridPoints;
    int nextPointId;
    GridPoint *selectedPoint;
    bool pointDragActive = false;
    QPointF pointDragOffset;
    QGraphicsPixmapItem *imageItem;
    QImage backgroundImage;
    bool imageMode;
    
    // Theme colors
    bool isDarkMode;
    QColor bgColor;
    QColor minorGridColor;
    QColor majorGridColor;
    QColor axisColor;
    QColor centerPointColor;
    QColor borderColor;
    QColor textColor;
    
    void updateThemeColors();
};

#endif // TARGETGRIDWIDGET_H
