#include "notifier.h"

#include "utils/logging.h"
#include "utils/settings.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>

#include <stdexcept>

static Logger logger = Logger::getLogger("Notifier");

Notifier::Notifier(const fs::path& root_path, const Cache& do_not_disturb,
                   const std::map<fs::path, int>& notification_backoff_minutes,
                   NotificationFolder& notification_cache, const QString& run_id, QObject* parent)
    : QObject(parent)
    , m_root_path(root_path)
    , m_do_not_disturb(do_not_disturb)
    , m_notification_backoff_minutes(notification_backoff_minutes)
    , m_notification_cache(notification_cache)
    , m_run_id(run_id) {
    m_started_at = QDateTime::currentDateTimeUtc();
    logger.info(QString("Started notifier with root path %1")
                    .arg(QString::fromStdString(root_path.string())));
}

void Notifier::notify(const CachedNotification& notification, bool is_batch) {
    std::vector<CachedNotification> vec = {notification};
    notify(vec, is_batch);
}

void Notifier::notify(const std::vector<CachedNotification>& notifications, bool is_batch) {
    try {
        logger.info(QString("Got request to display %1 notifications").arg(notifications.size()));

        std::vector<CachedNotification> to_display;
        for (const auto& n : notifications) {
            if (n.trashed)
                continue;

            int urgency = n.hints.value("urgency", 1).toInt();
            if (urgency == 2) {
                to_display.push_back(n);
                continue;
            }

            bool dnd_active =
                Settings::isDoNotDisturbActive(m_root_path, n.path.parent_path(), m_do_not_disturb);
            if (dnd_active && !is_batch)
                continue;

            int backoff = Settings::getNotificationBackoffMinutes(m_root_path, n.path.parent_path(),
                                                                  m_notification_backoff_minutes);
            if (!is_batch && backoff > 0)
                continue;

            to_display.push_back(n);
        }

        logger.info(QString("%1 to display").arg(to_display.size()));

        if (to_display.empty())
            return;

        const CachedNotification& last = to_display.back();

        QString summary =
            to_display.size() == 1
                ? last.summary
                : QString("%1 new notifications from %2").arg(to_display.size()).arg(last.app_name);

        QString body;
        if (to_display.size() == 1) {
            body = last.body;
        } else {
            QStringList parts;
            for (const auto& n : to_display) {
                QStringList content;
                if (!n.summary.isEmpty())
                    content << n.summary;
                if (!n.body.isEmpty())
                    content << n.body;
                parts << content.join("\n");
            }
            body = parts.join("\n---\n");
        }

        if (body.length() >= 1000) {
            body = body.left(997) + "...";
        }

        playNotificationSound(last);

        CachedNotification display_notif = last;
        display_notif.summary = summary;
        display_notif.body = body;

        NotificationWidget* widget = new NotificationWidget(display_notif);

        connect(widget, &NotificationWidget::closed, [this, widget, is_batch](int reason) {
            closeNotification(widget, static_cast<NotificationCloseReason>(reason), is_batch);
        });

        connect(widget, &NotificationWidget::snoozed,
                [this, widget](int duration_ms) { snoozeNotification(widget, duration_ms); });

        if (display_notif.notification_tray_run_id == m_run_id) {
            connect(widget, &NotificationWidget::actionInvoked, [this, widget](const QString& key) {
                emit actionInvoked(widget->data.id, key);
            });
        }

        showOrQueueNotification(widget);

        for (const auto& n : to_display) {
            m_last_notified[n.path.parent_path()] =
                std::max(m_last_notified[n.path.parent_path()], n.id);
        }
    } catch (const std::exception& e) {
        // Display error notification like Python does
        logger.error(QString("Unable to read notifications: %1").arg(e.what()));

        CachedNotification error_notif;
        error_notif.summary = "Error";
        error_notif.body = QString("Unable to read notifications: %1").arg(e.what());
        error_notif.app_icon = "error";
        error_notif.app_name = "notification-tray";
        error_notif.expire_timeout = -1;
        error_notif.id = -1;
        error_notif.replaces_id = 0;
        error_notif.at = QDateTime::currentDateTimeUtc();
        error_notif.hints["sound-name"] = "dialog-error";
        error_notif.notification_tray_run_id = m_run_id;
        error_notif.path = m_root_path / "error.json";

        // Recursively call notify with error notification
        notify(error_notif, false);
    }
}

void Notifier::showOrQueueNotification(NotificationWidget* widget) {
    logger.info(QString("Got request to show notification %1").arg(widget->data.id));

    m_notification_widgets[{widget->data.notification_tray_run_id, widget->data.id}] = widget;

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        logger.error("No screen available");
        return;
    }

    QRect geometry = screen->availableGeometry();

    QVariant x_hint = widget->data.hints.value("x");
    QVariant y_hint = widget->data.hints.value("y");
    if (!x_hint.isNull() && !y_hint.isNull()) {
        widget->move(x_hint.toInt(), y_hint.toInt());
    } else {
        if (geometry.height() - widget->height() - m_offset > 0) {
            widget->move(geometry.width() - widget->width(),
                         geometry.height() - widget->height() - m_offset);
            m_offset += widget->height() + 10;
            widget->show();

            if (widget->data.notification_tray_run_id == m_run_id) {
                connect(widget, &NotificationWidget::displayed, [this, widget]() {
                    emit notificationDisplayed(widget->data.id, widget->data.app_name,
                                               widget->data.summary, widget->data.body);
                });
            }
            emit widget->displayed();
        } else {
            logger.debug(
                QString("No screen space for notification %1. Queuing").arg(widget->data.id));
        }
    }
}

void Notifier::closeNotification(int notification_id, NotificationCloseReason reason,
                                 bool is_batch) {
    auto it = m_notification_widgets.find({m_run_id, notification_id});
    if (it == m_notification_widgets.end()) {
        logger.error(QString("Could not find notification with id %1").arg(notification_id));
        return;
    }
    closeNotification(it->second, reason, is_batch);
}

void Notifier::closeNotification(NotificationWidget* widget, NotificationCloseReason reason,
                                 bool is_batch) {
    auto key = std::make_pair(widget->data.notification_tray_run_id, widget->data.id);

    // Check if this notification has already been closed and removed
    auto it = m_notification_widgets.find(key);
    if (it == m_notification_widgets.end()) {
        logger.debug(QString("Notification %1 already closed, skipping").arg(widget->data.id));
        return;
    }

    logger.info(QString("Closing notification %1").arg(widget->data.id));

    if (widget->isVisible()) {
        widget->close();
    }

    if (reason != NotificationCloseReason::CLOSED_BY_CALL_TO_CLOSENOTIFICATION) {
        emit notificationClosed(widget->data.id, static_cast<int>(reason),
                                QString::fromStdString(widget->data.path.string()),
                                widget->data.notification_tray_run_id == m_run_id);
    }

    // Remove the widget from the map and schedule it for deletion
    m_notification_widgets.erase(it);
    widget->deleteLater();

    // Recalculate offset based on remaining visible widgets
    m_offset = 0;
    for (auto& [k, w] : m_notification_widgets) {
        if (w->isVisible()) {
            m_offset += w->height() + 10;
        }
    }

    // Show queued notifications that weren't displayed yet
    for (auto& [k, w] : m_notification_widgets) {
        if (!w->was_displayed) {
            showOrQueueNotification(w);
        }
    }
}

void Notifier::batchNotify() {
    // Implementation for batch notification processing
    // Includes post-DnD catchup logic from Python version

    std::function<void(NotificationFolder&)> process = [&](NotificationFolder& folder) {
        std::vector<CachedNotification> new_notifications;

        // Initialize last_notified for this folder if not present
        if (m_last_notified.find(folder.path) == m_last_notified.end()) {
            m_last_notified[folder.path] = -1;
        }

        for (auto& [name, notif] : folder.notifications) {
            if (notif.trashed)
                continue;

            int backoff = Settings::getNotificationBackoffMinutes(m_root_path, folder.path,
                                                                  m_notification_backoff_minutes);
            qint64 minutes_since = notif.at.secsTo(QDateTime::currentDateTimeUtc()) / 60;

            bool dnd_active =
                Settings::isDoNotDisturbActive(m_root_path, folder.path, m_do_not_disturb);

            if (!dnd_active) {
                bool should_notify = false;

                // Check if within backoff window
                if (backoff > 0 && minutes_since <= backoff) {
                    should_notify = true;
                }

                // Check if we just came back from DnD (post-DnD catchup)
                auto do_not_disturb =
                    Settings::getDoNotDisturb(m_root_path, folder.path, m_do_not_disturb);
                if (do_not_disturb.has_value()) {
                    QDateTime dnd_end = do_not_disturb.value();
                    // DnD ended after we started AND notification arrived after DnD ended
                    // AND we haven't notified about this yet
                    if (dnd_end >= m_started_at && notif.at >= dnd_end &&
                        notif.id > m_last_notified[folder.path]) {
                        should_notify = true;
                    }
                }

                if (should_notify) {
                    new_notifications.push_back(notif);
                }
            }
        }

        if (!new_notifications.empty()) {
            notify(new_notifications, true);
        }

        for (auto& [name, subfolder] : folder.folders) {
            process(subfolder);
        }
    };

    process(m_notification_cache);
}

void Notifier::playNotificationSound(const CachedNotification& notification) {
    if (notification.hints.value("suppress-sound", false).toBool()) {
        logger.debug(QString("Notification %1 has suppress-sound hint").arg(notification.id));
        return;
    }

    QString audio_path;

    if (notification.hints.contains("sound-file")) {
        audio_path = notification.hints["sound-file"].toString();
    } else if (notification.hints.contains("sound-name")) {
        QString sound_name = notification.hints["sound-name"].toString();
        audio_path = QString("/usr/share/sounds/freedesktop/%1.oga").arg(sound_name);
    } else {
        fs::path current = notification.path.parent_path();
        while (current != m_root_path.parent_path()) {
            fs::path sound_file = current / ".notification.wav";
            if (fs::exists(sound_file)) {
                audio_path = QString::fromStdString(sound_file.string());
                break;
            }
            current = current.parent_path();
        }
    }

    if (!audio_path.isEmpty() && QFile::exists(audio_path)) {
        logger.debug(QString("Playing notification sound: %1").arg(audio_path));
        QSoundEffect* sound = new QSoundEffect(this);
        sound->setSource(QUrl::fromLocalFile(audio_path));
        sound->play();
        // Clean up after playing
        connect(sound, &QSoundEffect::playingChanged, [sound]() {
            if (!sound->isPlaying()) {
                sound->deleteLater();
            }
        });
    }
}

void Notifier::snoozeNotification(NotificationWidget* widget, int duration_ms) {
    logger.info(QString("Snoozing notification %1 for %2 seconds")
                    .arg(widget->data.id)
                    .arg(duration_ms / 1000.0));

    // Create a timer to re-display the notification
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);

    // Copy the notification data
    CachedNotification notification_data = widget->data;

    // When timer expires, create a new notification widget and display it
    connect(timer, &QTimer::timeout, [this, notification_data]() {
        logger.info(QString("Re-displaying snoozed notification %1").arg(notification_data.id));

        NotificationWidget* new_widget = new NotificationWidget(notification_data);

        connect(new_widget, &NotificationWidget::closed, [this, new_widget](int reason) {
            closeNotification(new_widget, static_cast<NotificationCloseReason>(reason), false);
        });

        connect(new_widget, &NotificationWidget::snoozed, [this, new_widget](int duration_ms) {
            snoozeNotification(new_widget, duration_ms);
        });

        if (new_widget->data.notification_tray_run_id == m_run_id) {
            connect(new_widget, &NotificationWidget::actionInvoked,
                    [this, new_widget](const QString& key) {
                        emit actionInvoked(new_widget->data.id, key);
                    });
        }

        showOrQueueNotification(new_widget);
    });

    timer->start(duration_ms);
}
