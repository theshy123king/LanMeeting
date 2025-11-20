#ifndef CHATMESSAGEWIDGET_H
#define CHATMESSAGEWIDGET_H

#include <QWidget>

class QLabel;
class QResizeEvent;

class ChatMessageWidget : public QWidget
{
    Q_OBJECT

public:
    enum class MessageKind {
        Local,
        Remote,
        System
    };

    explicit ChatMessageWidget(const QString &sender,
                               const QString &message,
                               MessageKind kind,
                               QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupUi(const QString &sender,
                 const QString &message,
                 MessageKind kind);

    QLabel *m_bubbleLabel = nullptr;
};

#endif // CHATMESSAGEWIDGET_H
