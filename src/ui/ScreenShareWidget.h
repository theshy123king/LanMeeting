#ifndef SCREENSHAREWIDGET_H
#define SCREENSHAREWIDGET_H

#include <QWidget>
#include <QImage>

class ScreenShareWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenShareWidget(QWidget *parent = nullptr);

    void setFrame(const QImage &image);
    void clearFrame();
    bool hasFrame() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_frame;
};

#endif // SCREENSHAREWIDGET_H

