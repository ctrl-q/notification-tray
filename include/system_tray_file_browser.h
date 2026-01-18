#pragma once

#include "notification_cacher.h"
#include "notification_service.h"
#include "notifier.h"
#include "tray.h"
#include "utils/settings.h"

#include <QApplication>
#include <QTimer>

#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

class SystemTrayFileBrowser : public QApplication {
    Q_OBJECT

public:
    explicit SystemTrayFileBrowser(int& argc, char** argv, const fs::path& root_path);

signals:
    void applicationStarted();

private slots:
    void closeNotificationFromDbusCall(int id, int reason);
    void trashFromDbusCall(int id, int reason);
    void closeIfInThisRun(int id, int reason, const QString& path, bool is_in_this_run);
    void trashIfClosed(int, int reason, const QString& path, bool);

private:
    void trySetLxqtThemes();
    QString loadQss(const QString& qssFile);
    void startTimer();
    void refreshSettings();

    QString m_run_id;
    fs::path m_root_path;
    Cache m_do_not_disturb;
    Cache m_hide_from_tray;
    std::map<fs::path, int> m_notification_backoff_minutes;

    std::unique_ptr<NotificationService> m_notification_service;
    NotificationFolder m_notification_cache;
    std::unique_ptr<Notifier> m_notifier;
    std::unique_ptr<NotificationCacher> m_notification_cacher;
    std::unique_ptr<Tray> m_tray;
    QTimer* m_timer;
};
