#include "targetgridwidget.h"
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsPixmapItem>
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
#include <cmath>

namespace {
double chooseGridSpacing(double minSceneSpacing) {
    if (!(minSceneSpacing > 0.0) || !std::isfinite(minSceneSpacing)) {
        return 50.0;
    }

    const double exponent = std::floor(std::log10(minSceneSpacing));
    const double base = std::pow(10.0, exponent);
    const double normalized = minSceneSpacing / base;

    if (normalized <= 1.0) {
        return 1.0 * base;
    }
    if (normalized <= 2.0) {
        return 2.0 * base;
    }
    if (normalized <= 5.0) {
        return 5.0 * base;
    }
    return 10.0 * base;
}

int firstGridLine(double start, double spacing) {
    return static_cast<int>(std::floor(start / spacing) * spacing);
}

double ensureMaxGridLines(double spacing, double extent, int maxLines) {
    if (!(spacing > 0.0) || !(extent > 0.0) || maxLines <= 0) {
        return spacing;
    }

    const double estimatedLines = extent / spacing;
    if (estimatedLines <= static_cast<double>(maxLines)) {
        return spacing;
    }

    return chooseGridSpacing(extent / static_cast<double>(maxLines));
}
}

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
    constexpr qreal kPointPad = 2.5;
    constexpr qreal kLabelWidth = 24.0;
    constexpr qreal kLabelHeight = 16.0;
    constexpr qreal kLabelVisualOffset = 12.0;

    const QRectF pointRect(-POINT_RADIUS - kPointPad,
                           -POINT_RADIUS - kPointPad,
                           2.0 * (POINT_RADIUS + kPointPad),
                           2.0 * (POINT_RADIUS + kPointPad));
    const QRectF labelRect(-kLabelWidth / 2.0,
                           POINT_RADIUS + kLabelVisualOffset,
                           kLabelWidth,
                           kLabelHeight);
    return pointRect.united(labelRect);
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

    // Draw point ID text for all points: 40% opacity normally, full opacity when selected.
    QColor labelColor = selected ? QColor(0, 255, 0) : QColor(100, 150, 255);
    labelColor.setAlpha(selected ? 255 : 102); // 102/255 ~= 40%
    painter->setPen(QPen(labelColor));
    painter->drawText(QRectF(-12.0, -(POINT_RADIUS + 28.0), 24.0, 16.0),
                      Qt::AlignCenter,
                      QString::number(pointId));
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
      nextPointId(1), selectedPoint(nullptr), imageItem(nullptr),
      currentDisplayMode(DisplayMode::GridOnly), isDarkMode(true) {

    // Initialize with dark mode colors
    updateThemeColors();

    // Create a scene centered on the camera frame. The rendered view stays upright;
    // logical target coordinates convert Y separately so the UI still reports Cartesian values.
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    gridScene = new QGraphicsScene(this);
    gridScene->setSceneRect(-halfWidth, -halfHeight, cameraWidth, cameraHeight);
    setScene(gridScene);

    // Add an image item spanning the same scene coordinates as the grid.
    imageItem = new QGraphicsPixmapItem();
    imageItem->setOffset(-halfWidth, -halfHeight);
    imageItem->setVisible(false);
    imageItem->setZValue(-1.0);
    imageItem->setAcceptedMouseButtons(Qt::NoButton);
    gridScene->addItem(imageItem);

    // Set rendering hints
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Set view properties
    setDragMode(QGraphicsView::ScrollHandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setAlignment(Qt::AlignCenter);
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
    addPoint(pixelCoords, nextPointId);
}

void TargetGridWidget::addPoint(QPointF pixelCoords, int pointId) {
    // pixelCoords are logical centered coordinates with +Y upward.
    // Clamp coordinates to camera grid bounds: -width/2 to +width/2, -height/2 to +height/2
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    pixelCoords.setX(qBound(-halfWidth, pixelCoords.x(), halfWidth));
    pixelCoords.setY(qBound(-halfHeight, pixelCoords.y(), halfHeight));

    GridPoint *point = new GridPoint(pixelCoords, pointId);
    point->setPos(pixelToScene(pixelCoords));

    gridScene->addItem(point);
    gridPoints.append(point);

    emit pointAdded(pointId, pixelCoords);

    nextPointId = qMax(nextPointId, pointId + 1);
}

void TargetGridWidget::removePoint(int pointId) {
    for (int i = 0; i < gridPoints.size(); ++i) {
        if (gridPoints[i]->getPointId() == pointId) {
            if (selectedPoint == gridPoints[i]) {
                selectedPoint = nullptr;
                pointDragActive = false;
            }
            gridScene->removeItem(gridPoints[i]);
            delete gridPoints[i];
            gridPoints.removeAt(i);
            emit pointRemoved(pointId);

            // Keep IDs intuitive after deletions:
            // if empty restart at 1, otherwise continue from current max + 1.
            if (gridPoints.isEmpty()) {
                nextPointId = 1;
            } else {
                int maxPointId = 0;
                for (const GridPoint *point : gridPoints) {
                    if (point && point->getPointId() > maxPointId) {
                        maxPointId = point->getPointId();
                    }
                }
                nextPointId = maxPointId + 1;
            }
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
    pointDragActive = false;
    nextPointId = 1;
}

bool TargetGridWidget::hasPoint(int pointId) const {
    for (const GridPoint *point : gridPoints) {
        if (point && point->getPointId() == pointId) {
            return true;
        }
    }
    return false;
}

bool TargetGridWidget::selectPoint(int pointId) {
    GridPoint *targetPoint = nullptr;
    for (GridPoint *point : gridPoints) {
        if (point && point->getPointId() == pointId) {
            targetPoint = point;
            break;
        }
    }

    if (!targetPoint) {
        return false;
    }

    if (selectedPoint == targetPoint && targetPoint->isSelected()) {
        return true;
    }

    for (GridPoint *point : gridPoints) {
        if (point) {
            point->setSelected(point == targetPoint);
        }
    }

    selectedPoint = targetPoint;
    pointDragActive = false;
    emit pointSelected(pointId);
    return true;
}

bool TargetGridWidget::movePointById(int pointId, int deltaX, int deltaY) {
    for (GridPoint *point : gridPoints) {
        if (!point || point->getPointId() != pointId) {
            continue;
        }

        QPointF currentPixel = point->getPixelCoordinates();
        QPointF newPixel(currentPixel.x() + deltaX, currentPixel.y() + deltaY);

        const double halfWidth = cameraWidth / 2.0;
        const double halfHeight = cameraHeight / 2.0;
        newPixel.setX(qBound(-halfWidth, newPixel.x(), halfWidth));
        newPixel.setY(qBound(-halfHeight, newPixel.y(), halfHeight));

        if (newPixel == currentPixel) {
            if (selectedPoint != point || !point->isSelected()) {
                selectPoint(pointId);
            }
            return true;
        }

        if (selectedPoint != point || !point->isSelected()) {
            selectPoint(pointId);
        }

        point->setPixelCoordinates(newPixel);
        point->setPos(pixelToScene(newPixel));
        emit pointMoved(pointId, newPixel);
        return true;
    }

    return false;
}

bool TargetGridWidget::setPointCoordinates(int pointId, const QPointF &pixelCoords) {
    const double halfWidth = cameraWidth / 2.0;
    const double halfHeight = cameraHeight / 2.0;
    QPointF clamped = pixelCoords;
    clamped.setX(qBound(-halfWidth, clamped.x(), halfWidth));
    clamped.setY(qBound(-halfHeight, clamped.y(), halfHeight));

    for (GridPoint *point : gridPoints) {
        if (point && point->getPointId() == pointId) {
            point->setPixelCoordinates(clamped);
            point->setPos(pixelToScene(clamped));
            return true;
        }
    }

    return false;
}

QVector<QPair<int, QPointF>> TargetGridWidget::getAllPoints() const {
    QVector<QPair<int, QPointF>> result;
    for (const GridPoint *point : gridPoints) {
        result.append({point->getPointId(), point->getPixelCoordinates()});
    }
    return result;
}

void TargetGridWidget::setGridResolution(int cameraWidth, int cameraHeight) {
    if (cameraWidth <= 0 || cameraHeight <= 0) {
        return;
    }

    if (this->cameraWidth == cameraWidth && this->cameraHeight == cameraHeight) {
        updateBackgroundPixmap();
        return;
    }

    this->cameraWidth = cameraWidth;
    this->cameraHeight = cameraHeight;
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    gridScene->setSceneRect(-halfWidth, -halfHeight, cameraWidth, cameraHeight);

    for (GridPoint *point : gridPoints) {
        if (!point) {
            continue;
        }

        QPointF clamped = point->getPixelCoordinates();
        clamped.setX(qBound(-halfWidth, clamped.x(), halfWidth));
        clamped.setY(qBound(-halfHeight, clamped.y(), halfHeight));
        point->setPixelCoordinates(clamped);
        point->setPos(pixelToScene(clamped));
    }

    updateBackgroundPixmap();

    fitGridToView();
}

void TargetGridWidget::setBackgroundImage(const QImage &imgGrayCameraSized) {
    if (!imageItem) {
        return;
    }

    if (imgGrayCameraSized.isNull()) {
        clearBackgroundImage();
        return;
    }

    backgroundImage = imgGrayCameraSized;
    updateBackgroundPixmap();
    scene()->update();
}

void TargetGridWidget::clearBackgroundImage() {
    backgroundImage = QImage();
    if (imageItem) {
        imageItem->setPixmap(QPixmap());
        imageItem->setOffset(-cameraWidth / 2.0, -cameraHeight / 2.0);
        imageItem->setVisible(false);
    }
    scene()->update();
}

void TargetGridWidget::setDisplayMode(DisplayMode mode) {
    currentDisplayMode = mode;

    if (!isInteractiveDisplayMode()) {
        deselectAllPoints();
    }

    for (GridPoint *point : gridPoints) {
        point->setVisible(currentDisplayMode != DisplayMode::StaticImage);
    }

    if (imageItem) {
        imageItem->setVisible(currentDisplayMode != DisplayMode::GridOnly && !backgroundImage.isNull());
    }

    scene()->update();
}

bool TargetGridWidget::isInteractiveDisplayMode() const {
    return currentDisplayMode != DisplayMode::StaticImage;
}

void TargetGridWidget::updateBackgroundPixmap() {
    if (!imageItem) {
        return;
    }

    if (backgroundImage.isNull()) {
        imageItem->setPixmap(QPixmap());
        imageItem->setOffset(-cameraWidth / 2.0, -cameraHeight / 2.0);
        imageItem->setVisible(false);
        return;
    }

    const QImage grayscaleImage = backgroundImage.convertToFormat(QImage::Format_Grayscale8);
    const QImage fittedImage = grayscaleImage.scaled(
        cameraWidth,
        cameraHeight,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);

    imageItem->setPixmap(QPixmap::fromImage(fittedImage));
    imageItem->setOffset(-fittedImage.width() / 2.0, -fittedImage.height() / 2.0);
    imageItem->setVisible(currentDisplayMode != DisplayMode::GridOnly);
}

void TargetGridWidget::deselectAllPoints() {
    for (GridPoint *point : gridPoints) {
        point->setSelected(false);
    }
    selectedPoint = nullptr;
    pointDragActive = false;
    emit pointDeselected();
}

QPointF TargetGridWidget::screenToPixel(QPointF screenPos) const {
    return sceneToPixel(mapToScene(screenPos.toPoint()));
}

QPointF TargetGridWidget::pixelToScreen(QPointF pixelPos) const {
    return mapFromScene(pixelToScene(pixelPos));
}

QPointF TargetGridWidget::sceneToPixel(QPointF scenePos) const {
    return QPointF(scenePos.x(), -scenePos.y());
}

QPointF TargetGridWidget::pixelToScene(QPointF pixelPos) const {
    return QPointF(pixelPos.x(), -pixelPos.y());
}

void TargetGridWidget::fitGridToView() {
    if (fitGridInProgress) {
        return;
    }

    // Fit the camera grid to fully fill the viewport while keeping the rendered view upright.
    QRectF sceneRect = gridScene->sceneRect();
    if (sceneRect.isEmpty() || viewport()->width() <= 0 || viewport()->height() <= 0) {
        return;
    }

    // Calculate independent scale factors for full fill (no letterboxing).
    double scaleX = viewport()->width() / sceneRect.width();
    double scaleY = viewport()->height() / sceneRect.height();

    const QTransform current = transform();
    const bool sameScale =
        std::abs(current.m11() - scaleX) < 1e-6 &&
        std::abs(current.m22() - scaleY) < 1e-6 &&
        std::abs(current.m12()) < 1e-6 &&
        std::abs(current.m21()) < 1e-6;

    if (sameScale) {
        return;
    }

    // Apply non-uniform scaling without flipping the rendered image.
    fitGridInProgress = true;
    setTransform(QTransform::fromScale(scaleX, scaleY), false);
    centerOn(0, 0); // Center on the origin
    fitGridInProgress = false;
}

void TargetGridWidget::centerView() {
    // Center on the origin (0,0)
    centerOn(0, 0);
}

void TargetGridWidget::mousePressEvent(QMouseEvent *event) {
    if (!isInteractiveDisplayMode()) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        GridPoint *clickedPoint = nullptr;
        const QList<QGraphicsItem *> itemsAtCursor = gridScene->items(scenePos, Qt::IntersectsItemShape, Qt::DescendingOrder, QTransform());
        for (QGraphicsItem *item : itemsAtCursor) {
            if (GridPoint *point = dynamic_cast<GridPoint *>(item)) {
                clickedPoint = point;
                break;
            }
        }

        // If no item clicked, create new point
        if (!clickedPoint) {
            pointDragActive = false;
            QPointF pixelCoords = screenToPixel(event->pos());
            addPoint(pixelCoords);
            event->accept();
            return;
        }

        deselectAllPoints();
        clickedPoint->setSelected(true);
        selectedPoint = clickedPoint;
        emit pointSelected(clickedPoint->getPointId());

        pointDragActive = true;
        pointDragOffset = selectedPoint->getPixelCoordinates() - sceneToPixel(scenePos);
        event->accept();
        return;
    }

    pointDragActive = false;
    QGraphicsView::mousePressEvent(event);
}

void TargetGridWidget::mouseMoveEvent(QMouseEvent *event) {
    if (!isInteractiveDisplayMode() || !pointDragActive || !selectedPoint || !(event->buttons() & Qt::LeftButton)) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    QPointF newPixel = screenToPixel(event->pos()) + pointDragOffset;

    // Clamp to camera grid bounds: -width/2 to +width/2, -height/2 to +height/2
    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    newPixel.setX(qBound(-halfWidth, newPixel.x(), halfWidth));
    newPixel.setY(qBound(-halfHeight, newPixel.y(), halfHeight));

    if (selectedPoint->getPixelCoordinates() != newPixel) {
        selectedPoint->setPixelCoordinates(newPixel);
        selectedPoint->setPos(pixelToScene(newPixel));
        emit pointMoved(selectedPoint->getPointId(), newPixel);
    }

    event->accept();
}

void TargetGridWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        pointDragActive = false;
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void TargetGridWidget::keyPressEvent(QKeyEvent *event) {
    QGraphicsView::keyPressEvent(event);
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
    const bool hasImageBackground = currentDisplayMode != DisplayMode::GridOnly && !backgroundImage.isNull();

    // Draw background
    if (!hasImageBackground) {
        painter->fillRect(rect, bgColor);
    } else {
        painter->fillRect(rect, Qt::black);
    }

    if (!hasImageBackground && currentDisplayMode != DisplayMode::StaticImage) {
        drawGridOverlay(painter, rect, false);
    }
}

void TargetGridWidget::drawForeground(QPainter *painter, const QRectF &rect) {
    if (currentDisplayMode == DisplayMode::LiveCamera) {
        drawGridOverlay(painter, rect, true);
    }
    QGraphicsView::drawForeground(painter, rect);
}

void TargetGridWidget::drawGridOverlay(QPainter *painter, const QRectF &rect, bool hasImageBackground) const {
    const bool overlayGrid = currentDisplayMode != DisplayMode::StaticImage;
    if (!overlayGrid) {
        return;
    }

    double halfWidth = cameraWidth / 2.0;
    double halfHeight = cameraHeight / 2.0;
    const QRectF sceneBounds(-halfWidth, -halfHeight, cameraWidth, cameraHeight);
    const QRectF visibleRect = rect.intersected(sceneBounds);
    if (visibleRect.isEmpty()) {
        return;
    }

    const double scaleX = std::abs(transform().m11());
    const double scaleY = std::abs(transform().m22());
    const double safeScaleX = scaleX > 1e-6 ? scaleX : 1.0;
    const double safeScaleY = scaleY > 1e-6 ? scaleY : 1.0;
    const double minorSpacingX = chooseGridSpacing(36.0 / safeScaleX);
    const double minorSpacingY = chooseGridSpacing(36.0 / safeScaleY);
    constexpr int kMaxMinorLinesPerAxis = 160;
    constexpr int kMaxMajorLinesPerAxis = 60;
    const double boundedMinorSpacingX = ensureMaxGridLines(minorSpacingX, visibleRect.width(), kMaxMinorLinesPerAxis);
    const double boundedMinorSpacingY = ensureMaxGridLines(minorSpacingY, visibleRect.height(), kMaxMinorLinesPerAxis);
    const double majorSpacingX = ensureMaxGridLines(boundedMinorSpacingX * 4.0, visibleRect.width(), kMaxMajorLinesPerAxis);
    const double majorSpacingY = ensureMaxGridLines(boundedMinorSpacingY * 4.0, visibleRect.height(), kMaxMajorLinesPerAxis);

    QColor minorColor = minorGridColor;
    QColor majorColor = majorGridColor;
    QColor localAxisColor = axisColor;
    QColor localCenterColor = centerPointColor;
    QColor localBorderColor = borderColor;
    QColor localTextColor = textColor;
    QColor axisLabelColor = isDarkMode ? QColor(120, 180, 255) : QColor(70, 130, 180);

    if (hasImageBackground) {
        minorColor.setAlpha(isDarkMode ? 75 : 85);
        majorColor.setAlpha(isDarkMode ? 125 : 145);
        localAxisColor.setAlpha(205);
        localCenterColor.setAlpha(230);
        localBorderColor.setAlpha(185);
        localTextColor = QColor(240, 240, 240, 220);
        axisLabelColor = QColor(195, 225, 255, 230);
    }

    painter->setPen(QPen(minorColor, 0.3));
    for (double x = firstGridLine(visibleRect.left(), boundedMinorSpacingX); x <= visibleRect.right(); x += boundedMinorSpacingX) {
        if (x < sceneBounds.left() || x > sceneBounds.right()) {
            continue;
        }
        painter->drawLine(QLineF(x, visibleRect.top(), x, visibleRect.bottom()));
    }
    for (double y = firstGridLine(visibleRect.top(), boundedMinorSpacingY); y <= visibleRect.bottom(); y += boundedMinorSpacingY) {
        if (y < sceneBounds.top() || y > sceneBounds.bottom()) {
            continue;
        }
        painter->drawLine(QLineF(visibleRect.left(), y, visibleRect.right(), y));
    }

    painter->setPen(QPen(majorColor, 0.8));
    for (double x = firstGridLine(visibleRect.left(), majorSpacingX); x <= visibleRect.right(); x += majorSpacingX) {
        if (x < sceneBounds.left() || x > sceneBounds.right()) {
            continue;
        }
        painter->drawLine(QLineF(x, visibleRect.top(), x, visibleRect.bottom()));
    }
    for (double y = firstGridLine(visibleRect.top(), majorSpacingY); y <= visibleRect.bottom(); y += majorSpacingY) {
        if (y < sceneBounds.top() || y > sceneBounds.bottom()) {
            continue;
        }
        painter->drawLine(QLineF(visibleRect.left(), y, visibleRect.right(), y));
    }

    painter->setPen(QPen(localAxisColor, 1.0));
    if (visibleRect.top() <= 0.0 && visibleRect.bottom() >= 0.0) {
        painter->drawLine(QLineF(sceneBounds.left(), 0, sceneBounds.right(), 0));
    }
    if (visibleRect.left() <= 0.0 && visibleRect.right() >= 0.0) {
        painter->drawLine(QLineF(0, sceneBounds.top(), 0, sceneBounds.bottom()));
    }

    if (visibleRect.contains(QPointF(0, 0))) {
        painter->setPen(QPen(localCenterColor, 2.0));
        painter->drawEllipse(QPointF(0, 0), 3, 3);
    }

    painter->setPen(QPen(localBorderColor, 1.5));
    painter->drawRect(sceneBounds);

    painter->save();
    painter->resetTransform();
    painter->setPen(QPen(localTextColor));
    painter->setFont(QFont("Arial", 8, QFont::Bold));

    const int halfWidthInt = static_cast<int>(halfWidth);
    const int halfHeightInt = static_cast<int>(halfHeight);
    const QRect viewRect = viewport()->rect().adjusted(6, 6, -6, -6);

    painter->drawText(viewRect, Qt::AlignLeft | Qt::AlignTop,
                      QString("(-%1, %2)").arg(halfWidthInt).arg(halfHeightInt));
    painter->drawText(viewRect, Qt::AlignRight | Qt::AlignTop,
                      QString("(%1, %2)").arg(halfWidthInt).arg(halfHeightInt));
    painter->drawText(viewRect, Qt::AlignLeft | Qt::AlignBottom,
                      QString("(-%1, -%2)").arg(halfWidthInt).arg(halfHeightInt));
    painter->drawText(viewRect, Qt::AlignRight | Qt::AlignBottom,
                      QString("(%1, -%2)").arg(halfWidthInt).arg(halfHeightInt));

    painter->setFont(QFont("Arial", 8));
    painter->drawText(viewRect, Qt::AlignCenter, "(0, 0)");

    painter->setPen(QPen(axisLabelColor));
    painter->drawText(QRect(viewRect.right() - 24, viewRect.center().y() - 10, 20, 20), Qt::AlignCenter, "X");
    painter->drawText(QRect(viewRect.center().x() + 4, viewRect.top(), 20, 20), Qt::AlignCenter, "Y");
    painter->restore();
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
