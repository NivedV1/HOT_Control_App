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
#include <QTransform>

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

TargetGridWidget::TargetGridWidget(int cameraWidth, int cameraHeight, QWidget *parent)
    : QGraphicsView(parent), cameraWidth(cameraWidth), cameraHeight(cameraHeight),
      nextPointId(1), selectedPoint(nullptr), isDarkMode(true) {

    // Initialize with dark mode colors
    updateThemeColors();

    // Create scene with centered Cartesian coordinate system
    // (0,0) at center, X increases right, Y increases up (four quadrants)
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    gridScene = new QGraphicsScene(this);
    gridScene->setSceneRect(-halfWidth, -halfHeight, cameraWidth, cameraHeight);
    setScene(gridScene);

    // Set rendering hints
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Set view properties
    setDragMode(QGraphicsView::ScrollHandDrag);
    // Styling is now handled by the QSS theme files (light_theme.qss and theme.qss)

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
    // pixelCoords are in centered Cartesian coordinate system from screenToPixel
    // Clamp coordinates to camera grid bounds: -width/2 to +width/2, -height/2 to +height/2
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
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

void TargetGridWidget::setGridResolution(int cameraWidth, int cameraHeight) {
    this->cameraWidth = cameraWidth;
    this->cameraHeight = cameraHeight;
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    gridScene->setSceneRect(-halfWidth, -halfHeight, cameraWidth, cameraHeight);
    fitGridToView();
}

void TargetGridWidget::moveSelectedPoint(int deltaX, int deltaY) {
    if (!selectedPoint) return;

    QPointF currentPixel = selectedPoint->getPixelCoordinates();
    QPointF newPixel(currentPixel.x() + deltaX, currentPixel.y() + deltaY);

    // Clamp to camera grid bounds: -width/2 to +width/2, -height/2 to +height/2
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
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
    // Fit the entire camera resolution grid to fill the view
    QRectF sceneRect = gridScene->sceneRect();

    // Calculate scale factors to fill the viewport
    double scaleX = viewport()->width() / sceneRect.width();
    double scaleY = viewport()->height() / sceneRect.height();
    double scale = qMin(scaleX, scaleY);

    // Set the transform to fit and center
    setTransform(QTransform::fromScale(scale, scale));
    centerOn(0, 0); // Center on the origin
}

void TargetGridWidget::centerView() {
    // Center on the origin (0,0)
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
    painter->fillRect(rect, bgColor);

    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;

    // Show full grid with appropriate spacing for camera resolution
    int majorGridSpacing = 200;  // Major lines every 200 pixels
    int minorGridSpacing = 50;   // Minor lines every 50 pixels

    // Draw minor grid lines (finer grid)
    painter->setPen(QPen(minorGridColor, 0.3));
    for (int x = -static_cast<int>(halfWidth); x <= static_cast<int>(halfWidth); x += minorGridSpacing) {
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }
    for (int y = -static_cast<int>(halfHeight); y <= static_cast<int>(halfHeight); y += minorGridSpacing) {
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }

    // Draw major grid lines (coarser grid)
    painter->setPen(QPen(majorGridColor, 0.8));
    for (int x = -static_cast<int>(halfWidth); x <= static_cast<int>(halfWidth); x += majorGridSpacing) {
        painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
    }
    for (int y = -static_cast<int>(halfHeight); y <= static_cast<int>(halfHeight); y += majorGridSpacing) {
        painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
    }

    // Draw X and Y axes (centered Cartesian coordinate system)
    painter->setPen(QPen(axisColor, 1.0));
    painter->drawLine(QLineF(-halfWidth, 0, halfWidth, 0));  // X axis through center
    painter->drawLine(QLineF(0, -halfHeight, 0, halfHeight));  // Y axis through center

    // Draw origin point (0,0) at center
    painter->setPen(QPen(centerPointColor, 2.0));
    painter->drawEllipse(QPointF(0, 0), 3, 3);

    // Draw border
    painter->setPen(QPen(borderColor, 1.5));
    painter->drawRect(-halfWidth, -halfHeight, cameraWidth, cameraHeight);

    // Draw corner labels with Cartesian coordinates (four quadrants)
    painter->setPen(QPen(textColor));
    painter->setFont(QFont("Arial", 9, QFont::Bold));

    int halfWidthInt = static_cast<int>(halfWidth);
    int halfHeightInt = static_cast<int>(halfHeight);

    // Top-left corner (Q2: negative X, positive Y)
    painter->drawText(-halfWidthInt + 5, halfHeightInt - 20, 60, 20, Qt::AlignLeft | Qt::AlignTop,
                     QString("(-%1, %2)").arg(halfWidthInt).arg(halfHeightInt));

    // Top-right corner (Q1: positive X, positive Y)
    painter->drawText(halfWidthInt - 65, halfHeightInt - 20, 60, 20, Qt::AlignRight | Qt::AlignTop,
                     QString("(%1, %2)").arg(halfWidthInt).arg(halfHeightInt));

    // Bottom-left corner (Q3: negative X, negative Y)
    painter->drawText(-halfWidthInt + 5, -halfHeightInt + 5, 60, 20, Qt::AlignLeft | Qt::AlignBottom,
                     QString("(-%1, -%2)").arg(halfWidthInt).arg(halfHeightInt));

    // Bottom-right corner (Q4: positive X, negative Y)
    painter->drawText(halfWidthInt - 65, -halfHeightInt + 5, 60, 20, Qt::AlignRight | Qt::AlignBottom,
                     QString("(%1, -%2)").arg(halfWidthInt).arg(halfHeightInt));

    // Origin label
    painter->setFont(QFont("Arial", 8));
    painter->drawText(-20, -15, 40, 20, Qt::AlignCenter, "(0, 0)");

    // Draw axis labels
    painter->setFont(QFont("Arial", 8));
    painter->setPen(QPen(isDarkMode ? QColor(120, 180, 255) : QColor(70, 130, 180)));
    // X-axis label at right
    painter->drawText(halfWidthInt - 20, 8, 20, 16, Qt::AlignCenter, "X");
    // Y-axis label at top
    painter->drawText(5, halfHeightInt - 15, 20, 16, Qt::AlignCenter, "Y");
}

void TargetGridWidget::updateGridDisplay() {
    scene()->update();
}

void TargetGridWidget::setDarkMode(bool dark) {
    isDarkMode = dark;
    updateThemeColors();
    scene()->update();
}

void TargetGridWidget::updateThemeColors() {
    if (isDarkMode) {
        // Dark theme colors
        bgColor = QColor(26, 26, 26);
        minorGridColor = QColor(50, 50, 70);
        majorGridColor = QColor(80, 80, 120);
        axisColor = QColor(150, 100, 150);
        centerPointColor = QColor(200, 150, 200);
        borderColor = QColor(100, 100, 150);
        textColor = QColor(150, 150, 150);
    } else {
        // Light theme colors
        bgColor = QColor(255, 255, 255);
        minorGridColor = QColor(100, 120, 150);  // Darker minor grid lines for contrast
        majorGridColor = QColor(60, 100, 150);   // Darker major grid lines for contrast
        axisColor = QColor(40, 80, 130);
        centerPointColor = QColor(30, 70, 150);
        borderColor = QColor(60, 100, 150);
        textColor = QColor(60, 70, 90);
    }
}
