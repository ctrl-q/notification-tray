#include "tray.h"

#include "utils/logging.h"
#include "utils/settings.h"

#include <QAction>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>

static Logger logger = Logger::getLogger("Tray");

Tray::Tray(const fs::path& root_path, const Cache& do_not_disturb, const Cache& hide_from_tray,
           const std::map<fs::path, int>& notification_backoff_minutes, Notifier* notifier,
           NotificationCacher* notification_cacher, QApplication* app, QObject* parent)
    : QObject(parent)
    , m_root_path(root_path)
    , m_do_not_disturb(do_not_disturb)
    , m_hide_from_tray(hide_from_tray)
    , m_notification_backoff_minutes(notification_backoff_minutes)
    , m_notifier(notifier)
    , m_notification_cacher(notification_cacher)
    , m_app(app) {
    m_tray_menu = new QMenu();
    logger.info(
        QString("Started tray with root path %1").arg(QString::fromStdString(root_path.string())));
}

int Tray::countDir(NotificationFolder& dir) {
    if (Settings::isDoNotDisturbActive(m_root_path, dir.path, m_do_not_disturb) ||
        Settings::isHideFromTrayActive(m_root_path, dir.path, m_hide_from_tray)) {
        return 0;
    }

    int count = 0;
    int backoff = Settings::getNotificationBackoffMinutes(m_root_path, dir.path,
                                                          m_notification_backoff_minutes);

    for (const auto& [name, notif] : dir.notifications) {
        if (notif.trashed)
            continue;

        if (backoff <= 0) {
            count++;
        } else {
            qint64 minutes_since = notif.at.secsTo(QDateTime::currentDateTimeUtc()) / 60;
            if (minutes_since > backoff) {
                count++;
            }
        }
    }

    for (auto& [name, folder] : dir.folders) {
        count += countDir(folder);
    }

    return count;
}

bool Tray::hasNotifications(NotificationFolder& dir) {
    if (Settings::isDoNotDisturbActive(m_root_path, dir.path, m_do_not_disturb) ||
        Settings::isHideFromTrayActive(m_root_path, dir.path, m_hide_from_tray)) {
        return false;
    }

    for (const auto& [name, notif] : dir.notifications) {
        if (!notif.trashed) {
            return true;
        }
    }

    for (auto& [name, folder] : dir.folders) {
        if (hasNotifications(folder)) {
            return true;
        }
    }

    return false;
}

void Tray::updateIcon() {
    logger.debug("Updating tray icon");

    int file_count = countDir(m_notification_cacher->notification_cache);
    logger.info(QString("Tray icon update: %1 notifications").arg(file_count));

    if (file_count == 0) {
        if (m_tray_icon) {
            m_tray_icon->hide();
        }
    } else {
        if (!m_tray_icon) {
            m_tray_icon = new QSystemTrayIcon(m_app);
        }
        m_tray_icon->setIcon(getNotificationBadge(file_count));
        connect(m_tray_icon, &QSystemTrayIcon::activated, this, &Tray::setupTrayMenu);
        m_tray_icon->show();
        m_tray_icon->setContextMenu(m_tray_menu);
    }
}

QIcon Tray::getNotificationBadge(int number) {
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);

    QFont font;
    font.setPixelSize(24);
    font.setBold(true);
    painter.setFont(font);

    painter.setBrush(QColor(255, 0, 0));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(0, 0, 40, 40);

    painter.setPen(QColor(255, 255, 255));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString::number(number));

    painter.end();

    return QIcon(pixmap);
}

void Tray::setupTrayMenu() {
    m_tray_menu->clear();
    populateSubmenu(m_tray_menu, m_notification_cacher->notification_cache);

    QAction* exit_action = new QAction("Exit", m_tray_menu);
    connect(exit_action, &QAction::triggered, m_app, &QApplication::quit);
    m_tray_menu->addAction(exit_action);
}

void Tray::addDirectoryContents(const fs::path& path, QMenu* menu) {
    fs::path relative = fs::relative(path, m_root_path);
    NotificationFolder* current = &m_notification_cacher->notification_cache;

    for (const auto& part : relative) {
        QString folder_name = QString::fromStdString(part.string());
        if (current->folders.count(folder_name)) {
            current = &current->folders[folder_name];
        }
    }

    for (auto& [name, folder] : current->folders) {
        // Skip folders with no visible notifications
        if (!hasNotifications(folder)) {
            continue;
        }

        QMenu* submenu = new QMenu(name, menu);
        menu->addMenu(submenu);

        QAction* placeholder = new QAction("Loading...", submenu);
        submenu->addAction(placeholder);

        // Store the folder path to avoid dangling reference
        fs::path folder_path = folder.path;
        connect(submenu, &QMenu::aboutToShow, [this, submenu, folder_path]() {
            // Only populate if not already done (check for placeholder)
            QList<QAction*> actions = submenu->actions();
            if (actions.isEmpty() || actions[0]->text() != "Loading...") {
                return;  // Already populated
            }

            // Find the folder in the cache by path
            NotificationFolder* current_folder = &m_notification_cacher->notification_cache;
            fs::path relative = fs::relative(folder_path, m_root_path);
            for (const auto& part : relative) {
                QString folder_name = QString::fromStdString(part.string());
                if (current_folder->folders.count(folder_name)) {
                    current_folder = &current_folder->folders[folder_name];
                } else {
                    return;  // Folder no longer exists
                }
            }

            populateSubmenu(submenu, *current_folder);
        });
    }

    for (auto& [name, notif] : current->notifications) {
        if (!notif.trashed) {
            QAction* file_action = new QAction(name, menu);
            connect(file_action, &QAction::triggered,
                    [this, notif]() { m_notifier->notify(notif, true); });
            menu->addAction(file_action);
        }
    }
}

void Tray::populateSubmenu(QMenu* submenu, NotificationFolder& folder) {
    // Only populate if showing "Loading..." placeholder (matches Python behavior)
    QList<QAction*> actions = submenu->actions();
    if (!actions.isEmpty() && actions[0]->text() != "Loading...") {
        return;  // Already populated
    }

    submenu->clear();
    addDirectoryContents(folder.path, submenu);

    fs::path folder_path = folder.path;  // Capture by value to avoid dangling reference

    QAction* trash_action = new QAction("Move to Trash", submenu);
    connect(trash_action, &QAction::triggered,
            [this, folder_path]() { m_notification_cacher->trash(folder_path); });
    submenu->addAction(trash_action);

    QAction* show_all = new QAction("Show All", submenu);
    connect(show_all, &QAction::triggered, [this, folder_path]() {
        // Find the folder in the cache by path
        NotificationFolder* current_folder = &m_notification_cacher->notification_cache;
        fs::path relative = fs::relative(folder_path, m_root_path);
        for (const auto& part : relative) {
            QString folder_name = QString::fromStdString(part.string());
            if (current_folder->folders.count(folder_name)) {
                current_folder = &current_folder->folders[folder_name];
            } else {
                return;  // Folder no longer exists
            }
        }
        notifyFolder(*current_folder);
    });
    submenu->addAction(show_all);

    // Add "Do Not Disturb" submenu
    QMenu* dnd_menu = new QMenu("Do Not Disturb", submenu);
    submenu->addMenu(dnd_menu);

    QDateTime now = QDateTime::currentDateTimeUtc();
    QList<QPair<QString, QDateTime>> dnd_options = {
        {"1 hour", now.addSecs(3600)},
        {"8 hours", now.addSecs(28800)},
        {"Forever", QDateTime(QDate(9999, 1, 1), QTime(), Qt::UTC)}};

    for (const auto& [text, time] : dnd_options) {
        QAction* action = new QAction(text, dnd_menu);
        connect(action, &QAction::triggered, [this, folder_path, time]() {
            updateDateTimeSetting("do_not_disturb_until", folder_path, time, m_do_not_disturb);
        });
        dnd_menu->addAction(action);
    }

    // Add "Hide From Tray" submenu
    QMenu* hide_menu = new QMenu("Hide From Tray", submenu);
    submenu->addMenu(hide_menu);

    for (const auto& [text, time] : dnd_options) {
        QAction* action = new QAction(text, hide_menu);
        connect(action, &QAction::triggered, [this, folder_path, time]() {
            updateDateTimeSetting("hide_from_tray_until", folder_path, time, m_hide_from_tray);
        });
        hide_menu->addAction(action);
    }

    // Add "Batch Notifications" submenu
    QMenu* batch_menu = new QMenu("Batch Notifications", submenu);
    submenu->addMenu(batch_menu);

    QList<QPair<QString, int>> batch_options = {
        {"Every minute", 1}, {"Every 5 minutes", 5}, {"Every 10 minutes", 10}};

    for (const auto& [text, minutes] : batch_options) {
        QAction* action = new QAction(text, batch_menu);
        connect(action, &QAction::triggered, [this, folder_path, minutes]() {
            updateNotificationBackoffMinutes(folder_path, minutes);
        });
        batch_menu->addAction(action);
    }
}

void Tray::notifyFolder(NotificationFolder& folder) {
    std::vector<CachedNotification> notifications;

    std::function<void(NotificationFolder&)> collect = [&](NotificationFolder& f) {
        for (auto& [name, notif] : f.notifications) {
            notifications.push_back(notif);
        }
        for (auto& [name, subfolder] : f.folders) {
            collect(subfolder);
        }
    };

    collect(folder);
    m_notifier->notify(notifications, true);
}

void Tray::updateNotificationBackoffMinutes(const fs::path& folder_path, int minutes) {
    m_notification_backoff_minutes[folder_path] = minutes;

    fs::path settings_file = folder_path / ".settings.json";
    QString settings_path = QString::fromStdString(settings_file.string());

    QJsonObject existing_settings;
    QFile file(settings_path);
    if (file.open(QIODevice::ReadOnly)) {
        existing_settings = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    existing_settings["notification_backoff_minutes"] = minutes;

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(existing_settings).toJson());
        file.close();
    }
}

void Tray::updateDateTimeSetting(const QString& setting_name, const fs::path& folder_path,
                                 const QDateTime& until, Cache& cache) {
    Settings::writeDateTimeSetting(folder_path, setting_name, until, cache);
    Settings::cacheDateTimeSetting(folder_path, setting_name, cache);
    updateIcon();
    setupTrayMenu();
}

void Tray::refresh() {
    logger.info("Tray refresh called");
    updateIcon();
    setupTrayMenu();
}
