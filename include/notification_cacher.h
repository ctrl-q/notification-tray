#pragma once

#include "notification_types.h"
#include "utils/settings.h"

#include <QObject>

#include <filesystem>

namespace fs = std::filesystem;

class Notifier;

class NotificationCacher : public QObject {
    Q_OBJECT

public:
    explicit NotificationCacher(const fs::path& root_path, Notifier* notifier,
                                const Cache& do_not_disturb,
                                const std::map<fs::path, int>& notification_backoff_minutes,
                                NotificationFolder& notification_cache, const QString& run_id,
                                QObject* parent = nullptr);

    void cacheExistingNotifications(const fs::path& root_path);
    void cache(const CachedNotification& notification);
    void trash(const fs::path& path);

    NotificationFolder& notification_cache;

signals:
    void notificationsCached();
    void notificationTrashed(int id);

private:
    void markAsTrashed(NotificationFolder& folder);

    fs::path m_root_path;
    Notifier* m_notifier;
    Cache m_do_not_disturb;
    std::map<fs::path, int> m_notification_backoff_minutes;
    QString m_run_id;
};
