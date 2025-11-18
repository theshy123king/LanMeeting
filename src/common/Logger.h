#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

// Simple logging helpers that can later be wired to a richer backend.

void logInfo(const QString &message);
void logWarn(const QString &message);
void logError(const QString &message);

#define LOG_INFO(msg)  logInfo(QString(msg))
#define LOG_WARN(msg)  logWarn(QString(msg))
#define LOG_ERROR(msg) logError(QString(msg))

#endif // LOGGER_H

