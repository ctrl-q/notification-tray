#pragma once

#include "notification_types.h"
#include "notification_widget.h"
#include "utils/settings.h"

#include <QObject>
#include <QSoundEffect>

#include <filesystem>
#include <map>

namespace fs = std::filesystem;

class Notifier : public QObject {
    Q_OBJECT

public:
    explicit Notifier(const fs::path& root_path, const Cache& do_not_disturb,
                      const std::map<fs::path, int>& notification_backoff_minutes,
                      NotificationFolder& notification_cache, const QString& run_id,
                      QObject* parent = nullptr);

    void notify(const CachedNotification& notification, bool is_batch = false);
    void notify(const std::vector<CachedNotification>& notifications, bool is_batch = false);
    void closeNotification(int notification_id, NotificationCloseReason reason,
                           bool is_batch = false);
    void closeNotification(NotificationWidget* widget, NotificationCloseReason reason,
                           bool is_batch = false);
    void batchNotify();

signals:
    void actionInvoked(int id, const QString& key);
    void notificationDisplayed(int id, const QString& app_name, const QString& summary,
                               const QString& body);
    void notificationClosed(int id, int reason, const QString& path, bool is_in_this_run);

private:
    void showOrQueueNotification(NotificationWidget* notification_widget);
    void playNotificationSound(const CachedNotification& notification);
    void snoozeNotification(NotificationWidget* widget, int duration_ms);

    fs::path m_root_path;
    Cache m_do_not_disturb;
    std::map<fs::path, int> m_notification_backoff_minutes;
    NotificationFolder& m_notification_cache;
    QString m_run_id;
    QDateTime m_started_at;
    std::map<fs::path, int> m_last_notified;
    std::map<std::pair<QString, int>, NotificationWidget*> m_notification_widgets;
    int m_offset = 0;
};
