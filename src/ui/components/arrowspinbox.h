#ifndef ARROWSPINBOX_H
#define ARROWSPINBOX_H

#include <QDoubleSpinBox>
#include <QPainter>
#include <QPaintEvent>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>

namespace arrowspinbox_detail {
inline int luminance(const QColor &color) {
    return (299 * color.red() + 587 * color.green() + 114 * color.blue()) / 1000;
}

inline QColor arrowColor(const QPalette &palette, bool enabled) {
    QColor fg = palette.color(QPalette::ButtonText);
    const QColor bg = palette.color(QPalette::Button);

    if (qAbs(luminance(fg) - luminance(bg)) < 64) {
        fg = (luminance(bg) < 128) ? QColor(240, 240, 240) : QColor(30, 30, 30);
    }

    if (!enabled) {
        fg.setAlpha(120);
    }

    return fg;
}

inline QRect fallbackUpButtonRect(const QAbstractSpinBox *spinBox) {
    const int h = qMax(1, spinBox->height() - 2);
    const int w = qBound(16, h, 24);
    return QRect(spinBox->width() - (2 * w) - 1, 1, w, h); // left button
}

inline QRect fallbackDownButtonRect(const QAbstractSpinBox *spinBox) {
    const int h = qMax(1, spinBox->height() - 2);
    const int w = qBound(16, h, 24);
    return QRect(spinBox->width() - w - 1, 1, w, h); // right button
}

inline QRect resolveButtonRect(QAbstractSpinBox *spinBox,
                               const QStyleOptionSpinBox &opt,
                               bool upButton) {
    const QStyle::SubControl sc = upButton ? QStyle::SC_SpinBoxUp : QStyle::SC_SpinBoxDown;
    QRect r = spinBox->style()->subControlRect(QStyle::CC_SpinBox, &opt, sc, spinBox);
    if (!r.isValid() || r.width() < 8 || r.height() < 8) {
        r = upButton ? fallbackUpButtonRect(spinBox) : fallbackDownButtonRect(spinBox);
    }
    return r.intersected(spinBox->rect());
}

inline void drawTriangleInRect(QPainter &painter, const QRect &buttonRect, bool upButton) {
    if (!buttonRect.isValid()) {
        return;
    }

    const int side = qMax(8, qMin(buttonRect.width(), buttonRect.height()) / 2);
    const int half = side / 2;
    const QPoint c = buttonRect.center();

    QPolygon tri;
    if (upButton) {
        tri << QPoint(c.x(), c.y() - half)
            << QPoint(c.x() - half, c.y() + half)
            << QPoint(c.x() + half, c.y() + half);
    } else {
        tri << QPoint(c.x() - half, c.y() - half)
            << QPoint(c.x() + half, c.y() - half)
            << QPoint(c.x(), c.y() + half);
    }

    painter.drawPolygon(tri);
}

inline void drawArrows(QAbstractSpinBox *spinBox,
                       QPainter &painter,
                       const QStyleOptionSpinBox &opt,
                       bool enabled) {
    const QRect upRect = resolveButtonRect(spinBox, opt, true);
    const QRect downRect = resolveButtonRect(spinBox, opt, false);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(arrowColor(spinBox->palette(), enabled));

    drawTriangleInRect(painter, upRect, true);
    drawTriangleInRect(painter, downRect, false);

    painter.restore();
}
}

class ArrowSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;

protected:
    void paintEvent(QPaintEvent *event) override {
        QSpinBox::paintEvent(event);

        QStyleOptionSpinBox opt;
        initStyleOption(&opt);

        QPainter painter(this);
        arrowspinbox_detail::drawArrows(this, painter, opt, isEnabled());
    }
};

class ArrowDoubleSpinBox : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void paintEvent(QPaintEvent *event) override {
        QDoubleSpinBox::paintEvent(event);

        QStyleOptionSpinBox opt;
        initStyleOption(&opt);

        QPainter painter(this);
        arrowspinbox_detail::drawArrows(this, painter, opt, isEnabled());
    }
};

#endif // ARROWSPINBOX_H
