#include "notification_service.h"

#include "utils/logging.h"
#include "utils/paths.h"

#include <QDBusConnection>
#include <QDBusMessage>

static Logger logger = Logger::getLogger("NotificationService");

NotificationService::NotificationService(const fs::path& root_path, const QString& run_id,
                                         QObject* parent)
    : QObject(parent)
    , m_root_path(root_path)
    , m_run_id(run_id) {
    signaler = new NotificationServiceSignaler(this);

    QDBusConnection dbus = QDBusConnection::sessionBus();
    if (!dbus.registerService("org.freedesktop.Notifications")) {
        logger.error("Failed to register D-Bus service");
    }
    if (!dbus.registerObject("/org/freedesktop/Notifications", this,
                             QDBusConnection::ExportScriptableContents)) {
        logger.error("Failed to register D-Bus object");
    }

    logger.info(QString("Started notification service with root_path %1")
                    .arg(QString::fromStdString(root_path.string())));
}

uint NotificationService::Notify(const QString& app_name, uint replaces_id, const QString& app_icon,
                                 const QString& summary, const QString& body,
                                 const QStringList& actions, const QVariantMap& hints,
                                 int expire_timeout) {
    logger.info(QString("Got notification from %1 with summary %2").arg(app_name, summary));

    uint id = replaces_id ? replaces_id : static_cast<uint>(notifications.size() + 1);
    logger.info(QString("Notification ID: %1").arg(id));

    Notification notification;
    notification.app_name = app_name;
    notification.replaces_id = replaces_id;
    notification.app_icon = app_icon;
    notification.summary = summary;
    notification.body = body;
    notification.expire_timeout = expire_timeout;
    notification.id = id;
    notification.hints = hints;
    notification.at = QDateTime::currentDateTimeUtc();
    notification.notification_tray_run_id = m_run_id;

    for (int i = 0; i < actions.size(); i += 2) {
        if (i + 1 < actions.size()) {
            notification.actions[actions[i]] = actions[i + 1];
        }
    }

    CachedNotification cached;
    cached.app_name = notification.app_name;
    cached.replaces_id = notification.replaces_id;
    cached.app_icon = notification.app_icon;
    cached.summary = notification.summary;
    cached.body = notification.body;
    cached.expire_timeout = notification.expire_timeout;
    cached.id = notification.id;
    cached.actions = notification.actions;
    cached.hints = notification.hints;
    cached.at = notification.at;
    cached.notification_tray_run_id = notification.notification_tray_run_id;
    cached.path = Paths::getOutputPath(m_root_path, notification);

    notifications[id] = cached;
    emit signaler->notificationReady(id);

    logger.info(QString("Notification Ready. ID: %1").arg(id));
    return id;
}

void NotificationService::CloseNotification(uint id) {
    logger.info(QString("CloseNotification called for ID: %1").arg(id));

    auto it = notifications.find(id);
    if (it != notifications.end()) {
        it->second.closed_at = QDateTime::currentDateTimeUtc();
        if (!it->second.trashed) {
            auto reason =
                static_cast<uint>(NotificationCloseReason::CLOSED_BY_CALL_TO_CLOSENOTIFICATION);
            emit NotificationClosed(id, reason);
            emit signaler->notificationClosed(id, reason);
        }
    } else {
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs,
                           QString("Notification with id %1 not found").arg(id));
        }
        logger.error(QString("CloseNotification: ID %1 not found").arg(id));
    }
}

void NotificationService::CloseActiveNotifications() {
    logger.info("CloseActiveNotifications called");
    for (auto& [id, notification] : notifications) {
        // Only close notifications that are currently displayed (have an active widget)
        if (!notification.closed_at.has_value() && hasActiveWidget && hasActiveWidget(id)) {
            CloseNotification(id);
        }
    }
}

void NotificationService::OpenActiveNotifications() {
    logger.info("OpenActiveNotifications called");

    // Exclude empty action keys when counting.
    // Behavior:
    // - one non-empty action => invoke it
    // - multiple non-empty actions => error
    std::vector<std::pair<uint, QString>> ids_to_invoke;

    for (auto& [id, notification] : notifications) {
        if (!notification.closed_at.has_value()) {
            std::vector<QString> non_empty_action_keys;
            for (const auto& [key, value] : notification.actions) {
                Q_UNUSED(value);
                if (key.isEmpty()) {
                    continue;
                }
                non_empty_action_keys.push_back(key);
            }

            if (non_empty_action_keys.empty()) {
                // No actions, skip
                continue;
            }

            QString action_key;
            if (non_empty_action_keys.size() == 1) {
                action_key = non_empty_action_keys[0];
            }

            if (!action_key.isEmpty()) {
                ids_to_invoke.push_back({id, action_key});
            } else {
                if (calledFromDBus()) {
                    sendErrorReply(QDBusError::Failed,
                                   QString("Notification id %1 has more than one action").arg(id));
                }
                logger.error(
                    QString("OpenActiveNotifications: Notification %1 has more than one action")
                        .arg(id));
                return;
            }
        }
    }

    // Invoke all collected actions
    for (const auto& [id, action_key] : ids_to_invoke) {
        emit ActionInvoked(id, action_key);
    }
}

QStringList NotificationService::GetCapabilities() {
    return {"action-icons", "actions",     "body",        "body-hyperlinks",
            "body-images",  "body-markup", "persistence", "sound"};
}

void NotificationService::GetServerInformation(QString& name, QString& vendor, QString& version,
                                               QString& spec_version) {
    name = "notification-tray";
    vendor = "github.com";
    version = "0.0.1";
    spec_version = "1.3";
}
