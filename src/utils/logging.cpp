#include "utils/logging.h"

#include <QTextStream>

#include <cctype>
#include <cstdlib>
#include <iostream>

Logger::Level Logger::s_logLevel = Logger::LOG_LEVEL_INFO;
bool Logger::s_initialized = false;

// Use function-local statics to avoid static initialization order fiasco
static std::string& getLogFormat() {
    static std::string format = "{syslog_prefix}{timestamp} [{level}] {name}: {message}";
    return format;
}

static std::string& getTimestampFormat() {
    static std::string format = "yyyy-MM-ddTHH:mm:ss";
    return format;
}

// Dummy static variables to satisfy linker (not actually used)
std::string Logger::s_logFormat;
std::string Logger::s_timestampFormat;

Logger::Logger(const QString& name) : m_name(name) {
    if (!s_initialized) {
        initFromEnvironment();
    }
    openlog(m_name.toStdString().c_str(), LOG_PID, LOG_USER);
}

Logger::~Logger() {
    closelog();
}

void Logger::initFromEnvironment() {
    s_initialized = true;

    // NOTIFICATION_TRAY_LOG_FORMAT - format string with placeholders:
    //   {timestamp} - the formatted timestamp
    //   {level} - log level (DEBUG, INFO, WARNING, ERROR)
    //   {name} - logger name
    //   {message} - the log message
    //   {syslog_prefix} - syslog numeric prefix like <6>
    if (const char* format = std::getenv("NOTIFICATION_TRAY_LOG_FORMAT")) {
        getLogFormat() = format;
    }

    // NOTIFICATION_TRAY_LOG_TIMESTAMP_FORMAT - Qt datetime format string
    // Default: "yyyy-MM-ddTHH:mm:ss" (ISO format)
    // Examples: "HH:mm:ss", "yyyy-MM-dd HH:mm:ss.zzz", "ddd MMM d HH:mm:ss"
    if (const char* tsFormat = std::getenv("NOTIFICATION_TRAY_LOG_TIMESTAMP_FORMAT")) {
        getTimestampFormat() = tsFormat;
    }
}

void Logger::debug(const QString& message) {
    log(LOG_LEVEL_DEBUG, message);
}

void Logger::info(const QString& message) {
    log(LOG_LEVEL_INFO, message);
}

void Logger::warning(const QString& message) {
    log(LOG_LEVEL_WARNING, message);
}

void Logger::error(const QString& message) {
    log(LOG_LEVEL_ERROR, message);
}

void Logger::log(Level level, const QString& message) {
    if (level < s_logLevel)
        return;

    int syslogLevel = levelToSyslog(level);
    QString syslogPrefix = QString("<%1>").arg(syslogLevel);
    QString timestamp =
        QDateTime::currentDateTime().toString(QString::fromStdString(getTimestampFormat()));
    QString levelStr = levelToString(level);

    QString fullMessage = QString::fromStdString(getLogFormat());
    fullMessage.replace("{syslog_prefix}", syslogPrefix);
    fullMessage.replace("{timestamp}", timestamp);
    fullMessage.replace("{level}", levelStr);
    fullMessage.replace("{name}", m_name);
    fullMessage.replace("{message}", message);

    QTextStream stream(stdout);
    stream << fullMessage << Qt::endl;

    syslog(syslogLevel, "%s", message.toStdString().c_str());
}

Logger Logger::getLogger(const QString& name) {
    return Logger(name);
}

void Logger::setLogLevel(Level level) {
    s_logLevel = level;
}

int Logger::levelToSyslog(Level level) {
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return LOG_DEBUG;
    case LOG_LEVEL_INFO:
        return LOG_INFO;
    case LOG_LEVEL_WARNING:
        return LOG_WARNING;
    case LOG_LEVEL_ERROR:
        return LOG_ERR;
    default:
        return LOG_INFO;
    }
}

QString Logger::levelToString(Level level) {
    switch (level) {
    case LOG_LEVEL_DEBUG:
        return "DEBUG";
    case LOG_LEVEL_INFO:
        return "INFO";
    case LOG_LEVEL_WARNING:
        return "WARNING";
    case LOG_LEVEL_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
