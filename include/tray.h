#pragma once

#include "notification_cacher.h"
#include "notification_types.h"
#include "notifier.h"
#include "utils/settings.h"

#include <QApplication>
#include <QMenu>
#include <QSystemTrayIcon>

#include <filesystem>

namespace fs = std::filesystem;

class Tray : public QObject {
    Q_OBJECT

public:
    explicit Tray(const fs::path& root_path, const Cache& do_not_disturb,
                  const Cache& hide_from_tray,
                  const std::map<fs::path, int>& notification_backoff_minutes, Notifier* notifier,
                  NotificationCacher* notification_cacher, QApplication* app,
                  QObject* parent = nullptr);

    void refresh();

private:
    void updateIcon();
    void setupTrayMenu();
    void addDirectoryContents(const fs::path& path, QMenu* menu);
    void populateSubmenu(QMenu* submenu, NotificationFolder& folder);
    QIcon getNotificationBadge(int number);
    void notifyFolder(NotificationFolder& folder);
    void updateNotificationBackoffMinutes(const fs::path& folder_path, int minutes);
    void updateDateTimeSetting(const QString& setting_name, const fs::path& folder_path,
                               const QDateTime& until, Cache& cache);
    int countDir(NotificationFolder& dir);
    bool hasNotifications(NotificationFolder& dir);

    fs::path m_root_path;
    Cache m_do_not_disturb;
    Cache m_hide_from_tray;
    std::map<fs::path, int> m_notification_backoff_minutes;
    QSystemTrayIcon* m_tray_icon = nullptr;
    QMenu* m_tray_menu;
    Notifier* m_notifier;
    NotificationCacher* m_notification_cacher;
    QApplication* m_app;
};
