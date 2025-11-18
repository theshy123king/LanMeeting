#include "common/Logger.h"

#include <QDebug>

void logInfo(const QString &message)
{
    qDebug() << message;
}

void logWarn(const QString &message)
{
    qWarning() << message;
}

void logError(const QString &message)
{
    qCritical() << message;
}

