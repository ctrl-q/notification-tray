#include "system_tray_file_browser.h"

#include "utils/logging.h"
#include "utils/paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QUuid>

#include <csignal>

static Logger logger = Logger::getLogger("SystemTrayFileBrowser");

SystemTrayFileBrowser::SystemTrayFileBrowser(int& argc, char** argv, const fs::path& root_path)
    : QApplication(argc, argv)
    , m_root_path(root_path) {
    m_run_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    logger.info(QString("Starting application with root path %1")
                    .arg(QString::fromStdString(root_path.string())));

    refreshSettings();

    m_notification_service = std::make_unique<NotificationService>(m_root_path, m_run_id);
    trySetLxqtThemes();

    m_notification_cache.path = root_path;

    m_notifier =
        std::make_unique<Notifier>(m_root_path, m_do_not_disturb, m_notification_backoff_minutes,
                                   m_notification_cache, m_run_id, this);

    // Set callback for NotificationService to check if a notification has an active widget
    m_notification_service->hasActiveWidget = [this](int id) {
        return m_notifier->hasActiveWidget(id);
    };

    m_notification_cacher = std::make_unique<NotificationCacher>(
        m_root_path, m_notifier.get(), m_do_not_disturb, m_notification_backoff_minutes,
        m_notification_cache, m_run_id, this);

    connect(m_notification_service->signaler, &NotificationServiceSignaler::notificationReady,
            [this](int id) {
                auto& notif = m_notification_service->notifications[id];
                m_notification_cacher->cache(notif);
                m_notifier->notify(notif);
            });

    connect(m_notification_service->signaler, &NotificationServiceSignaler::notificationClosed,
            this, &SystemTrayFileBrowser::closeNotificationFromDbusCall);

    connect(m_notifier.get(), &Notifier::notificationDisplayed, m_notification_service.get(),
            &NotificationService::NotificationDisplayed);

    connect(m_notifier.get(), &Notifier::notificationClosed, this,
            &SystemTrayFileBrowser::closeIfInThisRun);

    connect(m_notifier.get(), &Notifier::notificationClosed, this,
            &SystemTrayFileBrowser::trashIfClosed);

    connect(m_notifier.get(), &Notifier::actionInvoked, [this](int id, const QString& key) {
        emit m_notification_service->ActionInvoked(id, key);
        m_notifier->closeNotification(id, NotificationCloseReason::DISMISSED_BY_USER);
    });

    m_tray = std::make_unique<Tray>(m_root_path, m_do_not_disturb, m_hide_from_tray,
                                    m_notification_backoff_minutes, m_notifier.get(),
                                    m_notification_cacher.get(), this, this);

    connect(m_notification_cacher.get(), &NotificationCacher::notificationsCached, m_tray.get(),
            &Tray::refresh);

    connect(m_notification_cacher.get(), &NotificationCacher::notificationTrashed,
            m_notification_service.get(), &NotificationService::NotificationPurged);

    startTimer();

    connect(this, &SystemTrayFileBrowser::applicationStarted,
            [this]() { m_notification_cacher->cacheExistingNotifications(m_root_path); });

    emit applicationStarted();
}

void SystemTrayFileBrowser::trySetLxqtThemes() {
    // XDG-compliant theme search paths (matching liblxqt's LXQtThemeData::findTheme)
    // Order: user data home, then system data dirs
    QStringList xdg_data_paths;

    // XDG_DATA_HOME (default: ~/.local/share)
    QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (dataHome.isEmpty()) {
        dataHome = QDir::homePath() + "/.local/share";
    }
    xdg_data_paths << dataHome;

    // XDG_DATA_DIRS (default: /usr/local/share:/usr/share)
    QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirs.isEmpty()) {
        dataDirs = "/usr/local/share:/usr/share";
    }
    xdg_data_paths << dataDirs.split(':');

    // Config file locations (for reading theme name)
    // LXQt uses XDG_CONFIG_HOME and XDG_CONFIG_DIRS
    QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configHome.isEmpty()) {
        configHome = QDir::homePath() + "/.config";
    }

    QStringList lxqt_config_files;
    lxqt_config_files << configHome + "/lxqt/lxqt.conf";

    QString configDirs = qEnvironmentVariable("XDG_CONFIG_DIRS");
    if (configDirs.isEmpty()) {
        configDirs = "/etc/xdg";
    }
    for (const QString& dir : configDirs.split(':')) {
        lxqt_config_files << dir + "/lxqt/lxqt.conf";
    }
    // Add /usr/share/lxqt as fallback (where system defaults often are)
    lxqt_config_files << "/usr/share/lxqt/lxqt.conf";

    logger.info(QString("Looking for LXQt config in: %1").arg(lxqt_config_files.join(", ")));

    // Read theme name from config files (user config takes priority)
    QString theme;
    QString icon_theme;
    QString qt_style;

    // Read in reverse order (system first, user overrides)
    for (int i = lxqt_config_files.size() - 1; i >= 0; --i) {
        const QString& config_file = lxqt_config_files[i];
        if (QFile::exists(config_file)) {
            QSettings config(config_file, QSettings::IniFormat);

            // Try reading with group first (standard LXQt format)
            config.beginGroup("General");
            QString t = config.value("theme").toString();
            QString it = config.value("icon_theme").toString();
            config.endGroup();

            // Qt style is under [Qt] group
            config.beginGroup("Qt");
            QString qs = config.value("style").toString();
            config.endGroup();

            // If that didn't work, try reading at top level
            if (t.isEmpty()) {
                t = config.value("theme").toString();
            }
            if (it.isEmpty()) {
                it = config.value("icon_theme").toString();
            }

            if (!t.isEmpty()) {
                theme = t;
            }
            if (!it.isEmpty()) {
                icon_theme = it;
            }
            if (!qs.isEmpty()) {
                qt_style = qs;
            }
        }
    }

    // Apply Qt style if found (this affects buttons, etc.)
    if (!qt_style.isEmpty()) {
        logger.info(QString("Found Qt style: %1").arg(qt_style));
        setStyle(QStyleFactory::create(qt_style));
    }

    // Apply theme if found
    if (!theme.isEmpty()) {
        logger.info(QString("Found theme: %1").arg(theme));

        // Find theme directory in XDG data paths (matching liblxqt's findTheme)
        QString themePath;
        for (const QString& dataPath : xdg_data_paths) {
            QString candidate = dataPath + "/lxqt/themes/" + theme;
            if (QDir(candidate).exists()) {
                themePath = candidate;
                break;
            }
        }

        if (!themePath.isEmpty()) {
            // Load the notification daemon stylesheet (matching liblxqt's LXQtTheme::qss)
            QString qssFile = themePath + "/lxqt-notificationd.qss";
            if (QFile::exists(qssFile)) {
                QString stylesheet = loadQss(qssFile);
                if (!stylesheet.isEmpty()) {
                    // Replace Notification with NotificationWidget since our class has a different
                    // name Use word boundary patterns to avoid partial replacements
                    stylesheet.replace(QRegularExpression("\\bNotification\\b"),
                                       "NotificationWidget");

                    logger.info(QString("Loaded stylesheet from %1").arg(qssFile));
                    setStyleSheet(stylesheet);
                }
            } else {
                logger.info(QString("No lxqt-notificationd.qss found in theme %1").arg(theme));
            }
        } else {
            logger.info(QString("Theme directory not found for: %1").arg(theme));
        }
    } else {
        logger.info("No LXQt theme configured");
    }

    // Apply icon theme if found
    if (!icon_theme.isEmpty()) {
        logger.info(QString("Found icon theme: %1").arg(icon_theme));
        QIcon::setThemeName(icon_theme);
    } else {
        logger.info("No LXQt icon theme configured");
    }
}

QString SystemTrayFileBrowser::loadQss(const QString& qssFile) {
    // Matching liblxqt's LXQtThemeData::loadQss implementation
    QFile f(qssFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString qss = QString::fromLocal8Bit(f.readAll());
    f.close();

    if (qss.isEmpty()) {
        return QString();
    }

    // Handle relative paths in url() - matching liblxqt's regex approach
    // Pattern matches "url(" with optional whitespace
    QRegularExpression urlRegexp("url\\s*\\(\\s*", QRegularExpression::CaseInsensitiveOption);
    QString qssDir = QFileInfo(qssFile).canonicalPath();
    qss.replace(urlRegexp, QString("url(%1/").arg(qssDir));

    return qss;
}

void SystemTrayFileBrowser::startTimer() {
    m_timer = new QTimer(this);
    m_timer->setInterval(60000);
    connect(m_timer, &QTimer::timeout, this, &SystemTrayFileBrowser::refreshSettings);
    connect(m_timer, &QTimer::timeout, m_tray.get(), &Tray::refresh);
    connect(m_timer, &QTimer::timeout, m_notifier.get(), &Notifier::batchNotify);
    m_timer->start();
}

void SystemTrayFileBrowser::refreshSettings() {
    m_do_not_disturb.clear();
    m_hide_from_tray.clear();
    m_notification_backoff_minutes.clear();

    for (const auto& entry : fs::recursive_directory_iterator(m_root_path)) {
        if (entry.path().filename() == ".settings.json") {
            Settings::cacheDateTimeSetting(entry.path().parent_path(), "do_not_disturb_until",
                                           m_do_not_disturb);
            Settings::cacheDateTimeSetting(entry.path().parent_path(), "hide_from_tray_until",
                                           m_hide_from_tray);

            QFile file(QString::fromStdString(entry.path().string()));
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("notification_backoff_minutes")) {
                        m_notification_backoff_minutes[entry.path().parent_path()] =
                            obj["notification_backoff_minutes"].toInt();
                    }
                }
            }
        }
    }
}

void SystemTrayFileBrowser::closeNotificationFromDbusCall(int id, int reason) {
    // Close the widget if it still exists
    if (m_notifier->hasActiveWidget(id)) {
        m_notifier->closeNotification(id, static_cast<NotificationCloseReason>(reason), false);
    }

    // Always trash the notification when CloseNotification is called directly
    auto& notif = m_notification_service->notifications[id];
    trashIfClosed(0, reason, QString::fromStdString(notif.path.string()), false);
}

void SystemTrayFileBrowser::closeIfInThisRun(int id, int reason, const QString& path,
                                             bool is_in_this_run) {
    if (is_in_this_run) {
        emit m_notification_service->NotificationClosed(id, reason);
    }
}

void SystemTrayFileBrowser::trashIfClosed(int, int reason, const QString& path, bool) {
    auto close_reason = static_cast<NotificationCloseReason>(reason);
    if (close_reason == NotificationCloseReason::CLOSED_BY_CALL_TO_CLOSENOTIFICATION ||
        close_reason == NotificationCloseReason::DISMISSED_BY_USER) {
        m_notification_cacher->trash(path.toStdString());
    }
}
