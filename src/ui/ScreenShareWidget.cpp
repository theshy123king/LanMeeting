#include "ScreenShareWidget.h"

#include <QPainter>

ScreenShareWidget::ScreenShareWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ScreenShareWidget::setFrame(const QImage &image)
{
    m_frame = image;
    update();
}

void ScreenShareWidget::clearFrame()
{
    m_frame = QImage();
    update();
}

bool ScreenShareWidget::hasFrame() const
{
    return !m_frame.isNull();
}

void ScreenShareWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (m_frame.isNull()) {
        return;
    }

    const QSize widgetSize = size();
    if (widgetSize.isEmpty()) {
        return;
    }

    QSize imageSize = m_frame.size();
    if (imageSize.isEmpty()) {
        return;
    }

    imageSize.scale(widgetSize, Qt::KeepAspectRatioByExpanding);

    const QPoint topLeft((widgetSize.width() - imageSize.width()) / 2,
                         (widgetSize.height() - imageSize.height()) / 2);
    const QRect targetRect(topLeft, imageSize);

    painter.drawImage(targetRect, m_frame);
}

