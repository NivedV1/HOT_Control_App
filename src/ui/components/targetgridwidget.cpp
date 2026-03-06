#include "targetgridwidget.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <qmath.h>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QResizeEvent>
#include <QShowEvent>

// Static constants
const double GridPoint::POINT_RADIUS = 5.0;

// ==========================================
// GridPoint Implementation
// ==========================================

GridPoint::GridPoint(QPointF pixelCoords, int pointId, QGraphicsItem *parent)
    : QGraphicsItem(parent), pixelCoordinates(pixelCoords), pointId(pointId),
      selected(false), isDragging(false) {
    setAcceptHoverEvents(true);
    setZValue(1.0);
}

QRectF GridPoint::boundingRect() const {
    return QRectF(-POINT_RADIUS - 2, -POINT_RADIUS - 2, 
                   2 * (POINT_RADIUS + 2), 2 * (POINT_RADIUS + 2));
}

void GridPoint::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    if (selected) {
        painter->setPen(QPen(QColor(0, 255, 0), 2.0));  // Green for selected
        painter->setBrush(QBrush(QColor(0, 200, 0, 100)));
    } else {
        painter->setPen(QPen(QColor(100, 150, 255), 1.5));  // Blue for normal
        painter->setBrush(QBrush(QColor(100, 150, 255, 150)));
    }
    
    painter->drawEllipse(QPointF(0, 0), POINT_RADIUS, POINT_RADIUS);
    
    // Draw point ID text
    if (selected) {
        painter->setPen(QPen(QColor(0, 255, 0)));
        painter->drawText(-10, POINT_RADIUS + 12, 20, 16, Qt::AlignCenter, QString::number(pointId));
    }
}

void GridPoint::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        isDragging = true;
        event->accept();
    }
}

void GridPoint::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (isDragging) {
        setPos(event->scenePos());
        event->accept();
    }
}

void GridPoint::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        isDragging = false;
        event->accept();
    }
}

// ==========================================
// TargetGridWidget Implementation
// ==========================================

TargetGridWidget::TargetGridWidget(int slmWidth, int slmHeight, QWidget *parent)
    : QGraphicsView(parent), slmWidth(slmWidth), slmHeight(slmHeight),
      nextPointId(1), selectedPoint(nullptr) {
    
    // Create scene with centered coordinate system
    // Center at (0,0), ranging from -width/2 to +width/2 and -height/2 to +height/2
    double halfWidth = slmWidth / 2.0;
    double halfHeight = slmHeight / 2.0;
    gridScene = new QGraphicsScene(this);
    gridScene->setSceneRect(-halfWidth, -halfHeight, slmWidth, slmHeight);
    setScene(gridScene);
    
    // Set rendering hints
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // Set view properties
    setDragMode(QGraphicsView::ScrollHandDrag);
    setStyleSheet("QGraphicsView { border: 1px solid #444; background: #1a1a1a; }");
    
    // Enable focus
    setFocus();
    setFocusPolicy(Qt::StrongFocus);
    
    // Fit the grid to view
    fitGridToView();
}

TargetGridWidget::~TargetGridWidget() {
    gridPoints.clear();
}

void TargetGridWidget::addPoint(QPointF pixelCoords) {
    // pixelCoords are already in centered coordinate system from screenToPixel
    // Clamp coordinates to grid bounds: -width/2 to +width/2, -height/2 to +height/2
    double halfWidth = slmWidth / 2.0;
    double halfHeight = slmHeight / 2.0;
    pixelCoords.setX(qBound(-halfWidth, pixelCoords.x(), halfWidth));
    pixelCoords.setY(qBound(-halfHeight, pixelCoords.y(), halfHeight));
    
    GridPoint *point = new GridPoint(pixelCoords, nextPointId);
    point->setPos(pixelCoords);
    
    gridScene->addItem(point);
    gridPoints.append(point);
    
    emit pointAdded(nextPointId, pixelCoords);
    
    nextPointId++;
}

void TargetGridWidget::removePoint(int pointId) {
    for (int i = 0; i < gridPoints.size(); ++i) {
        if (gridPoints[i]->getPointId() == pointId) {
            if (selectedPoint == gridPoints[i]) {
                selectedPoint = nullptr;
            }
            gridScene->removeItem(gridPoints[i]);
            delete gridPoints[i];
            gridPoints.removeAt(i);
            emit pointRemoved(pointId);
            break;
        }
    }
}

void TargetGridWidget::clearAllPoints() {
    for (GridPoint *point : gridPoints) {
        gridScene->removeItem(point);
        delete point;
    }
    gridPoints.clear();
    selectedPoint = nullptr;
    nextPointId = 1;
}

QVector<QPair<int, QPointF>> TargetGridWidget::getAllPoints() const {
    QVector<QPair<int, QPointF>> result;
    for (const GridPoint *point : gridPoints) {
        result.append({point->getPointId(), point->getPixelCoordinates()});
    }
    return result;
}

void TargetGridWidget::setGridResolution(int width, int height) {
    slmWidth = width;
    slmHeight = height;
    double halfWidth = slmWidth / 2.0;
    double halfHeight = slmHeight / 2.0;
    gridScene->setSceneRect(-halfWidth, -halfHeight, slmWidth, slmHeight);
    fitGridToView();
}

void TargetGridWidget::moveSelectedPoint(int deltaX, int deltaY) {
    if (!selectedPoint) return;
    
    QPointF currentPixel = selectedPoint->getPixelCoordinates();
    QPointF newPixel(currentPixel.x() + deltaX, currentPixel.y() + deltaY);
    
    // Clamp to grid bounds: -width/2 to +width/2, -height/2 to +height/2
    double halfWidth = slmWidth / 2.0;
    double halfHeight = slmHeight / 2.0;
    newPixel.setX(qBound(-halfWidth, newPixel.x(), halfWidth));
    newPixel.setY(qBound(-halfHeight, newPixel.y(), halfHeight));
    
    selectedPoint->setPixelCoordinates(newPixel);
    selectedPoint->setPos(newPixel);
    
    emit pointMoved(selectedPoint->getPointId(), newPixel);
}

void TargetGridWidget::deselectAllPoints() {
    for (GridPoint *point : gridPoints) {
        point->setSelected(false);
    }
    selectedPoint = nullptr;
    emit pointDeselected();
}

QPointF TargetGridWidget::screenToPixel(QPointF screenPos) const {
    return mapToScene(screenPos.toPoint());
}

QPointF TargetGridWidget::pixelToScreen(QPointF pixelPos) const {
    return mapFromScene(pixelPos);
}

void TargetGridWidget::fitGridToView() {
    // Fit and center the entire grid in the view
    fitInView(gridScene->sceneRect(), Qt::KeepAspectRatio);
    centerView();
}

void TargetGridWidget::centerView() {
    // Reset transformation to identity
    resetTransform();
    // Remove extra margin so grid fills the view
    QRectF sceneRect = gridScene->sceneRect();
    fitInView(sceneRect, Qt::KeepAspectRatio);
    centerOn(0, 0);
}

void TargetGridWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(event->pos());
        QGraphicsItem *item = gridScene->itemAt(scenePos, QTransform());
        
        // If no item clicked, create new point
        if (!item) {
            QPointF pixelCoords = screenToPixel(event->pos());
            addPoint(pixelCoords);
            event->accept();
            return;
        } else if (GridPoint *point = dynamic_cast<GridPoint*>(item)) {
            deselectAllPoints();
            point->setSelected(true);
            selectedPoint = point;
            emit pointSelected(point->getPointId());
            event->accept();
            return;
        }
    }
    
    QGraphicsView::mousePressEvent(event);
}

void TargetGridWidget::keyPressEvent(QKeyEvent *event) {
    if (!selectedPoint) {
        QGraphicsView::keyPressEvent(event);
        return;
    }
    
    int delta = 1;  // Movement step in pixels
    
    switch (event->key()) {
        case Qt::Key_Up:
            moveSelectedPoint(0, -delta);
            break;
        case Qt::Key_Down:
            moveSelectedPoint(0, delta);
            break;
        case Qt::Key_Left:
            moveSelectedPoint(-delta, 0);
            break;
        case Qt::Key_Right:
            moveSelectedPoint(delta, 0);
            break;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            if (selectedPoint) {
                removePoint(selectedPoint->getPointId());
                selectedPoint = nullptr;
            }
            break;
        default:
            QGraphicsView::keyPressEvent(event);
            return;
    }
    
    event->accept();
}

void TargetGridWidget::wheelEvent(QWheelEvent *event) {
    // Zoom with mouse wheel
    double scaleFactor = event->angleDelta().y() > 0 ? 1.1 : 0.9;
    scale(scaleFactor, scaleFactor);
    event->accept();
}

void TargetGridWidget::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    fitGridToView();
}

void TargetGridWidget::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    // Center the view when first shown
    centerView();
}

void TargetGridWidget::drawBackground(QPainter *painter, const QRectF &rect) {
    // Draw background
    painter->fillRect(rect, QColor(26, 26, 26));
    
    double halfWidth = slmWidth / 2.0;
    double halfHeight = slmHeight / 2.0;
    
    int majorGridSpacing = 100;
    int minorGridSpacing = 20;
    
    // Draw minor grid lines
    painter->setPen(QPen(QColor(50, 50, 70), 0.5));
    for (int x = -static_cast<int>(halfWidth); x <= static_cast<int>(halfWidth); x += minorGridSpacing) {
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }
    for (int y = -static_cast<int>(halfHeight); y <= static_cast<int>(halfHeight); y += minorGridSpacing) {
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }
    
    // Draw major grid lines
    painter->setPen(QPen(QColor(80, 80, 120), 1.0));
    for (int x = -static_cast<int>(halfWidth); x <= static_cast<int>(halfWidth); x += majorGridSpacing) {
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }
    for (int y = -static_cast<int>(halfHeight); y <= static_cast<int>(halfHeight); y += majorGridSpacing) {
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }
    
    // Draw X and Y axes (centered at 0,0)
    painter->setPen(QPen(QColor(150, 100, 150), 2.0));
    painter->drawLine(QLineF(-halfWidth, 0, halfWidth, 0));  // X axis
    painter->drawLine(QLineF(0, -halfHeight, 0, halfHeight));  // Y axis
    
    // Draw center point
    painter->setPen(QPen(QColor(200, 150, 200), 3.0));
    painter->drawEllipse(QPointF(0, 0), 3, 3);
    
    // Draw border
    painter->setPen(QPen(QColor(100, 100, 150), 1.5));
    painter->drawRect(-halfWidth, -halfHeight, slmWidth, slmHeight);
    
    // Draw corner labels with centered coordinates
    painter->setPen(QPen(QColor(150, 150, 150)));
    painter->setFont(QFont("Arial", 9, QFont::Bold));
    
    int minX = -static_cast<int>(halfWidth);
    int maxX = static_cast<int>(halfWidth);
    int minY = -static_cast<int>(halfHeight);
    int maxY = static_cast<int>(halfHeight);
    
    // Top-left corner label
    painter->drawText(-halfWidth + 5, -halfHeight + 5, 60, 20, Qt::AlignLeft | Qt::AlignTop, 
                     QString("(%1, %2)").arg(minX).arg(maxY));
    
    // Bottom-right corner label
    painter->drawText(halfWidth - 65, halfHeight - 20, 60, 20, Qt::AlignRight | Qt::AlignBottom, 
                     QString("(%1, %2)").arg(maxX).arg(minY));
    
    // Center label
    painter->setFont(QFont("Arial", 8));
    painter->drawText(-20, -15, 40, 20, Qt::AlignCenter, "(0, 0)");
    
    // Draw axis labels
    painter->setFont(QFont("Arial", 8));
    painter->setPen(QPen(QColor(120, 180, 255)));
    // X-axis label at right
    painter->drawText(halfWidth - 20, 8, 20, 16, Qt::AlignCenter, "X");
    // Y-axis label at top
    painter->drawText(5, -halfHeight + 5, 20, 16, Qt::AlignCenter, "Y");
}

void TargetGridWidget::updateGridDisplay() {
    scene()->update();
}
