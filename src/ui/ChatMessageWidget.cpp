#include "ChatMessageWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {
QString addSoftBreaks(const QString &text)
{
    QString result;
    result.reserve(text.size() * 2);
    int runLength = 0;

    for (const QChar ch : text) {
        result.append(ch);
        if (ch.isLetterOrNumber()) {
            ++runLength;
            if (runLength >= 10) {
                result.append(QChar(0x200B)); // zero-width space to allow wrapping
                runLength = 0;
            }
        } else {
            runLength = 0;
        }
    }

    return result;
}

QString toBubbleHtml(const QString &text)
{
    QString withBreaks = addSoftBreaks(text);
    QString safe = withBreaks.toHtmlEscaped();
    safe.replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
    return QStringLiteral("<span style='white-space:pre-wrap;'>%1</span>").arg(safe);
}

} // namespace

ChatMessageWidget::ChatMessageWidget(const QString &sender,
                                     const QString &message,
                                     ChatMessageWidget::MessageKind kind,
                                     QWidget *parent)
    : QWidget(parent)
{
    setupUi(sender, message, kind);
}

void ChatMessageWidget::setupUi(const QString &sender,
                                const QString &message,
                                ChatMessageWidget::MessageKind kind)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 4, 8, 4);
    outerLayout->setSpacing(2);

    m_bubbleLabel = new QLabel(this);
    m_bubbleLabel->setWordWrap(true);
    m_bubbleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_bubbleLabel->setTextFormat(Qt::RichText);
    m_bubbleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    m_bubbleLabel->setMaximumWidth(width() > 0 ? qMax(120, static_cast<int>(width() * 0.6)) : 360);

    auto makeRichText = [](const QString &text) {
        QString safe = text.toHtmlEscaped();
        safe.replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
        return QStringLiteral("<span style='word-break:break-all; white-space:pre-wrap;'>%1</span>").arg(safe);
    };

    if (kind == MessageKind::System) {
        m_bubbleLabel->setText(toBubbleHtml(message));
        m_bubbleLabel->setAlignment(Qt::AlignCenter);
        m_bubbleLabel->setStyleSheet(
            QStringLiteral("background-color:#e0e0e0;"
                           "color:#555555;"
                           "border-radius:12px;"
                           "padding:4px 10px;"
                           "font-size:11px;"
                           "word-break:break-word;"
                           "word-wrap:break-word;"
                           "white-space:pre-wrap;"));

        auto *row = new QHBoxLayout;
        row->addStretch();
        row->addWidget(m_bubbleLabel, 0, Qt::AlignCenter);
        row->addStretch();
        outerLayout->addLayout(row);
        return;
    }

    auto *nameLabel = new QLabel(this);
    nameLabel->setText(sender);
    nameLabel->setTextFormat(Qt::PlainText);
    nameLabel->setStyleSheet(
        QStringLiteral("color:#888888; font-size:10px;"));

    m_bubbleLabel->setText(toBubbleHtml(message));
    m_bubbleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    if (kind == MessageKind::Local) {
        nameLabel->setAlignment(Qt::AlignRight);
        m_bubbleLabel->setStyleSheet(
            QStringLiteral("background-color:#0b93f6;"
                           "color:#ffffff;"
                           "border-radius:12px;"
                           "padding:8px 12px;"
                           "word-break:break-word;"
                           "word-wrap:break-word;"
                           "white-space:pre-wrap;"));

        auto *nameRow = new QHBoxLayout;
        nameRow->addStretch();
        nameRow->addWidget(nameLabel, 0, Qt::AlignRight);

        auto *bubbleRow = new QHBoxLayout;
        bubbleRow->addStretch();
        bubbleRow->addWidget(m_bubbleLabel, 0, Qt::AlignRight);

        outerLayout->addLayout(nameRow);
        outerLayout->addLayout(bubbleRow);
    } else {
        nameLabel->setAlignment(Qt::AlignLeft);
        m_bubbleLabel->setStyleSheet(
            QStringLiteral("background-color:#e5e5ea;"
                           "color:#111111;"
                           "border-radius:12px;"
                           "padding:8px 12px;"
                           "word-break:break-word;"
                           "word-wrap:break-word;"
                           "white-space:pre-wrap;"));

        auto *nameRow = new QHBoxLayout;
        nameRow->addWidget(nameLabel, 0, Qt::AlignLeft);
        nameRow->addStretch();

        auto *bubbleRow = new QHBoxLayout;
        bubbleRow->addWidget(m_bubbleLabel, 0, Qt::AlignLeft);
        bubbleRow->addStretch();

        outerLayout->addLayout(nameRow);
        outerLayout->addLayout(bubbleRow);
    }
}

void ChatMessageWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_bubbleLabel) {
        return;
    }

    const int maxWidth =
        qMax(120, static_cast<int>(event->size().width() * 0.6));
    m_bubbleLabel->setMaximumWidth(maxWidth);
    m_bubbleLabel->updateGeometry();
}
