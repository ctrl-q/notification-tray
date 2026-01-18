#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

#include <filesystem>
#include <map>
#include <optional>

namespace fs = std::filesystem;

enum class NotificationCloseReason {
    EXPIRED = 1,
    DISMISSED_BY_USER = 2,
    CLOSED_BY_CALL_TO_CLOSENOTIFICATION = 3,
    UNDEFINED = 4
};

using NotificationHints = QVariantMap;

struct Notification {
    QString app_name;
    int replaces_id;
    QString app_icon;
    QString summary;
    QString body;
    int expire_timeout;
    int id;
    std::map<QString, QString> actions;
    NotificationHints hints;
    QDateTime at;
    QString notification_tray_run_id;
};

struct CachedNotification : public Notification {
    fs::path path;
    std::optional<QDateTime> closed_at;
    bool trashed = false;
};

struct NotificationFolder {
    std::map<QString, NotificationFolder> folders;
    std::map<QString, CachedNotification> notifications;
    fs::path path;
};
