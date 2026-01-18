#pragma once

#include <QDateTime>
#include <QDebug>
#include <QString>

#include <string>
#include <syslog.h>

class Logger {
public:
    enum Level { LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARNING, LOG_LEVEL_ERROR };

    explicit Logger(const QString& name);
    ~Logger();

    void debug(const QString& message);
    void info(const QString& message);
    void warning(const QString& message);
    void error(const QString& message);
    void log(Level level, const QString& message);

    static Logger getLogger(const QString& name);
    static void setLogLevel(Level level);
    static void initFromEnvironment();

private:
    QString m_name;
    static Level s_logLevel;
    static std::string s_logFormat;
    static std::string s_timestampFormat;
    static bool s_initialized;

    int levelToSyslog(Level level);
    QString levelToString(Level level);
};
