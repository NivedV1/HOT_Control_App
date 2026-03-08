#ifndef ARROWSPINBOX_H
#define ARROWSPINBOX_H

#include <QDoubleSpinBox>
#include <QPainter>
#include <QPaintEvent>
#include <QSpinBox>

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

inline QRect upButtonRect(const QAbstractSpinBox *spinBox) {
    const int h = qMax(1, spinBox->height() - 2);
    const int w = qBound(16, h, 24);
    return QRect(spinBox->width() - w - 1, 1, w, h);
}

inline QRect downButtonRect(const QAbstractSpinBox *spinBox) {
    const int h = qMax(1, spinBox->height() - 2);
    const int w = qBound(16, h, 24);
    return QRect(spinBox->width() - (2 * w) - 1, 1, w, h);
}

inline void drawArrowText(QAbstractSpinBox *spinBox, QPainter &painter, bool enabled) {
    const QRect upRect = upButtonRect(spinBox).intersected(spinBox->rect());
    const QRect downRect = downButtonRect(spinBox).intersected(spinBox->rect());
    if (!upRect.isValid() || !downRect.isValid()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont f = painter.font();
    f.setBold(true);
    f.setPixelSize(qBound(10, spinBox->height() / 2, 16));
    painter.setFont(f);
    painter.setPen(arrowColor(spinBox->palette(), enabled));

    painter.drawText(upRect, Qt::AlignCenter, QString(QChar(0x25B2)));   // up triangle
    painter.drawText(downRect, Qt::AlignCenter, QString(QChar(0x25BC))); // down triangle

    painter.restore();
}
}

class ArrowSpinBox : public QSpinBox {
public:
    using QSpinBox::QSpinBox;

protected:
    void paintEvent(QPaintEvent *event) override {
        QSpinBox::paintEvent(event);
        QPainter painter(this);
        arrowspinbox_detail::drawArrowText(this, painter, isEnabled());
    }
};

class ArrowDoubleSpinBox : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void paintEvent(QPaintEvent *event) override {
        QDoubleSpinBox::paintEvent(event);
        QPainter painter(this);
        arrowspinbox_detail::drawArrowText(this, painter, isEnabled());
    }
};

#endif // ARROWSPINBOX_H
