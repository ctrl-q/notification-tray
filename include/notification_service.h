#pragma once

#include "notification_types.h"

#include <QDBusConnection>
#include <QObject>

#include <filesystem>
#include <functional>
#include <map>

namespace fs = std::filesystem;

class NotificationServiceSignaler : public QObject {
    Q_OBJECT

public:
    explicit NotificationServiceSignaler(QObject* parent = nullptr) : QObject(parent) {}

signals:
    void notificationReady(int id);
    void notificationClosed(int id, int reason);
};

class NotificationService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    explicit NotificationService(const fs::path& root_path, const QString& run_id,
                                 QObject* parent = nullptr);

    std::map<int, CachedNotification> notifications;
    NotificationServiceSignaler* signaler;

    // Callback to check if a notification has an active widget (set by SystemTrayFileBrowser)
    std::function<bool(int)> hasActiveWidget;

public slots:
    // Standard freedesktop.org Notifications methods (called by adaptor)
    Q_SCRIPTABLE uint Notify(const QString& app_name, uint replaces_id, const QString& app_icon,
                             const QString& summary, const QString& body,
                             const QStringList& actions, const QVariantMap& hints,
                             int expire_timeout);

    Q_SCRIPTABLE void CloseNotification(uint id);
    Q_SCRIPTABLE QStringList GetCapabilities();
    Q_SCRIPTABLE void GetServerInformation(QString& name, QString& vendor, QString& version,
                                           QString& spec_version);

    // Custom methods (exposed via D-Bus)
    Q_SCRIPTABLE void CloseActiveNotifications();
    Q_SCRIPTABLE void OpenActiveNotifications();

signals:
    // Standard freedesktop.org Notifications signals
    Q_SCRIPTABLE void ActionInvoked(uint id, const QString& action_key);
    Q_SCRIPTABLE void NotificationClosed(uint id, uint reason);

    // Custom signals
    Q_SCRIPTABLE void NotificationPurged(uint id);
    Q_SCRIPTABLE void NotificationDisplayed(uint id, const QString& app_name,
                                            const QString& summary, const QString& body);

private:
    fs::path m_root_path;
    QString m_run_id;
};
