#include "common/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace {
QMutex &logMutex()
{
    static QMutex mutex;
    return mutex;
}

QFile *ensureLogFile()
{
    static QFile *file = nullptr;

    if (!file) {
        file = new QFile(QStringLiteral("log.txt"), QCoreApplication::instance());
        if (!file->open(QIODevice::Append | QIODevice::Text)) {
            // If the log file cannot be opened, fall back to Qt debug output only.
            delete file;
            file = nullptr;
        }
    }

    return file;
}

void writeLogLine(const QString &level, const QString &message)
{
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
    const QString line = QStringLiteral("%1 [%2] %3").arg(timestamp, level, message);

    // Output to Qt debug stream
    if (level == QLatin1String("INFO")) {
        qDebug().noquote() << line;
    } else if (level == QLatin1String("WARN")) {
        qWarning().noquote() << line;
    } else {
        qCritical().noquote() << line;
    }

    // Append to log file
    QMutexLocker locker(&logMutex());
    QFile *file = ensureLogFile();
    if (!file) {
        return;
    }

    QTextStream stream(file);
    stream << line << '\n';
    stream.flush();
}
} // namespace

void logInfo(const QString &message)
{
    writeLogLine(QStringLiteral("INFO"), message);
}

void logWarn(const QString &message)
{
    writeLogLine(QStringLiteral("WARN"), message);
}

void logError(const QString &message)
{
    writeLogLine(QStringLiteral("ERROR"), message);
}
