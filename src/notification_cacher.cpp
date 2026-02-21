#include "notification_cacher.h"

#include "notifier.h"
#include "utils/logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <thread>

// Improved send2trash implementation using Qt
static bool send2trash(const fs::path& path) {
    QString qpath = QString::fromStdString(path.string());

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    // Qt 5.15+ has built-in moveToTrash
    return QFile::moveToTrash(qpath);
#else
    // Fallback: Move to freedesktop.org trash location
    QString trash_dir = QDir::homePath() + "/.local/share/Trash/files";
    QString info_dir = QDir::homePath() + "/.local/share/Trash/info";

    QDir().mkpath(trash_dir);
    QDir().mkpath(info_dir);

    QFileInfo file_info(qpath);
    QString filename = file_info.fileName();
    QString trash_path = trash_dir + "/" + filename;
    QString info_path = info_dir + "/" + filename + ".trashinfo";

    // Handle name conflicts
    int counter = 1;
    while (QFile::exists(trash_path)) {
        trash_path = trash_dir + "/" + filename + "." + QString::number(counter);
        info_path = info_dir + "/" + filename + "." + QString::number(counter) + ".trashinfo";
        counter++;
    }

    // Create .trashinfo file (freedesktop.org trash specification)
    QFile info_file(info_path);
    if (info_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&info_file);
        out << "[Trash Info]\n";
        out << "Path=" << file_info.absoluteFilePath() << "\n";
        out << "DeletionDate=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        info_file.close();
    }

    // Move file to trash
    if (QFile::exists(qpath)) {
        if (file_info.isDir()) {
            return QDir().rename(qpath, trash_path);
        } else {
            return QFile::rename(qpath, trash_path);
        }
    }

    return false;
#endif
}

static Logger logger = Logger::getLogger("NotificationCacher");

NotificationCacher::NotificationCacher(const fs::path& root_path, Notifier* notifier,
                                       const Cache& do_not_disturb,
                                       const std::map<fs::path, int>& notification_backoff_minutes,
                                       NotificationFolder& notification_cache,
                                       const QString& run_id, QObject* parent)
    : QObject(parent)
    , m_root_path(root_path)
    , m_notifier(notifier)
    , notification_cache(notification_cache)
    , m_do_not_disturb(do_not_disturb)
    , m_notification_backoff_minutes(notification_backoff_minutes)
    , m_run_id(run_id) {
    logger.info(QString("Started notification cacher with root path %1")
                    .arg(QString::fromStdString(root_path.string())));
}

void NotificationCacher::cacheExistingNotifications(const fs::path& root_path) {
    logger.info(QString("Caching existing notifications under %1")
                    .arg(QString::fromStdString(root_path.string())));

    for (const auto& entry : fs::recursive_directory_iterator(root_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json" &&
            entry.path().filename() != ".settings.json") {
            fs::path dirpath = entry.path().parent_path();
            fs::path relative = fs::relative(dirpath, root_path);

            NotificationFolder* current = &notification_cache;
            for (const auto& part : relative) {
                QString folder_name = QString::fromStdString(part.string());
                if (!current->folders.count(folder_name)) {
                    current->folders[folder_name].path = current->path / part;
                }
                current = &current->folders[folder_name];
            }

            QFile file(QString::fromStdString(entry.path().string()));
            if (file.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();

                    CachedNotification notif;
                    notif.app_name = obj["app_name"].toString();
                    notif.summary = obj["summary"].toString();
                    notif.body = obj["body"].toString();
                    notif.app_icon = obj["app_icon"].toString();
                    notif.id = obj["id"].toInt();
                    notif.replaces_id = obj["replaces_id"].toInt();
                    notif.expire_timeout = obj["expire_timeout"].toInt();
                    notif.notification_tray_run_id = obj["notification_tray_run_id"].toString();
                    notif.path = entry.path();

                    // Load actions from JSON
                    if (obj.contains("actions") && obj["actions"].isObject()) {
                        QJsonObject actions_obj = obj["actions"].toObject();
                        for (auto it = actions_obj.begin(); it != actions_obj.end(); ++it) {
                            notif.actions[it.key()] = it.value().toString();
                        }
                    }

                    // Load hints from JSON
                    if (obj.contains("hints") && obj["hints"].isObject()) {
                        QJsonObject hints_obj = obj["hints"].toObject();
                        for (auto it = hints_obj.begin(); it != hints_obj.end(); ++it) {
                            QJsonValue val = it.value();
                            if (val.isString()) {
                                notif.hints[it.key()] = val.toString();
                            } else if (val.isDouble()) {
                                // JSON doesn't distinguish int/double, check if it's a whole number
                                double d = val.toDouble();
                                if (d == static_cast<int>(d)) {
                                    notif.hints[it.key()] = static_cast<int>(d);
                                } else {
                                    notif.hints[it.key()] = d;
                                }
                            } else if (val.isBool()) {
                                notif.hints[it.key()] = val.toBool();
                            }
                        }
                    }

                    QFileInfo fileInfo(file);
                    notif.at = fileInfo.lastModified().toUTC();

                    current
                        ->notifications[QString::fromStdString(entry.path().filename().string())] =
                        notif;
                }
            }
        }
    }

    emit notificationsCached();
}

void NotificationCacher::cache(const CachedNotification& notification) {
    if (!notification.hints.value("transient", false).toBool()) {
        fs::create_directories(notification.path.parent_path());

        QJsonObject obj;
        obj["app_name"] = notification.app_name;
        obj["summary"] = notification.summary;
        obj["body"] = notification.body;
        obj["app_icon"] = notification.app_icon;
        obj["id"] = notification.id;
        obj["replaces_id"] = notification.replaces_id;
        obj["expire_timeout"] = notification.expire_timeout;
        obj["notification_tray_run_id"] = notification.notification_tray_run_id;

        // Serialize actions to JSON object
        QJsonObject actions_obj;
        for (auto it = notification.actions.begin(); it != notification.actions.end(); ++it) {
            actions_obj[it->first] = it->second;
        }
        obj["actions"] = actions_obj;

        // Serialize hints to JSON object
        QJsonObject hints_obj;
        for (auto it = notification.hints.begin(); it != notification.hints.end(); ++it) {
            QVariant value = it.value();
            // Convert QVariant to appropriate JSON type
            if (value.type() == QVariant::String) {
                hints_obj[it.key()] = value.toString();
            } else if (value.type() == QVariant::Int || value.type() == QVariant::LongLong) {
                hints_obj[it.key()] = value.toLongLong();
            } else if (value.type() == QVariant::UInt || value.type() == QVariant::ULongLong) {
                hints_obj[it.key()] = static_cast<qint64>(value.toULongLong());
            } else if (value.type() == QVariant::Double) {
                hints_obj[it.key()] = value.toDouble();
            } else if (value.type() == QVariant::Bool) {
                hints_obj[it.key()] = value.toBool();
            } else if (value.canConvert<QString>()) {
                hints_obj[it.key()] = value.toString();
            }
            // Skip complex types like image-data that can't be easily serialized
        }
        obj["hints"] = hints_obj;

        QFile file(QString::fromStdString(notification.path.string()));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(obj).toJson());
            logger.info(
                QString("Notification %1 written to %2")
                    .arg(notification.summary, QString::fromStdString(notification.path.string())));
        }
    }

    fs::path relative = fs::relative(notification.path.parent_path(), notification_cache.path);
    NotificationFolder* current = &notification_cache;
    for (const auto& part : relative) {
        QString folder_name = QString::fromStdString(part.string());
        if (!current->folders.count(folder_name)) {
            current->folders[folder_name].path = current->path / part;
        }
        current = &current->folders[folder_name];
    }

    current->notifications[QString::fromStdString(notification.path.filename().string())] =
        notification;
    emit notificationsCached();
}

void NotificationCacher::trash(const fs::path& path) {
    logger.info(QString("Trashing %1").arg(QString::fromStdString(path.string())));

    if (path == m_root_path) {
        for (auto& [name, subfolder] : notification_cache.folders) {
            std::thread([this, subfolder]() { trash(subfolder.path); }).detach();
        }
        for (auto& [name, notif] : notification_cache.notifications) {
            std::thread([this, notif]() { trash(notif.path); }).detach();
        }
        emit notificationsCached();
        return;
    }

    fs::path relative = fs::relative(path.parent_path(), m_root_path);
    NotificationFolder* current = &notification_cache;
    for (const auto& part : relative) {
        QString folder_name = QString::fromStdString(part.string());
        if (current->folders.count(folder_name)) {
            current = &current->folders[folder_name];
        }
    }

    if (fs::is_regular_file(path)) {
        if (path.extension() == ".json" && path.filename() != ".settings.json") {
            send2trash(path);
            QString filename = QString::fromStdString(path.filename().string());
            if (current->notifications.count(filename)) {
                current->notifications[filename].trashed = true;
                if (current->notifications[filename].notification_tray_run_id == m_run_id) {
                    emit notificationTrashed(current->notifications[filename].id);
                }
            }
        }
    } else if (!fs::exists(path)) {
        // Handle transient notifications that exist in cache but not on disk
        QString filename = QString::fromStdString(path.filename().string());
        if (current->notifications.count(filename)) {
            logger.info(QString("Marking transient notification %1 as trashed").arg(filename));
            current->notifications[filename].trashed = true;
            if (current->notifications[filename].notification_tray_run_id == m_run_id) {
                emit notificationTrashed(current->notifications[filename].id);
            }
        }
    } else {
        bool has_settings = false;
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.path().filename() == ".settings.json" ||
                entry.path().filename() == ".notification.wav") {
                has_settings = true;
                break;
            }
        }

        if (!has_settings) {
            send2trash(path);
            QString folder_name = QString::fromStdString(path.filename().string());
            if (current->folders.count(folder_name)) {
                markAsTrashed(current->folders[folder_name]);
            }
        } else {
            QString folder_name = QString::fromStdString(path.filename().string());
            if (current->folders.count(folder_name)) {
                NotificationFolder& folder = current->folders[folder_name];
                for (auto& [name, subfolder] : folder.folders) {
                    std::thread([this, subfolder]() { trash(subfolder.path); }).detach();
                }
                for (auto& [name, notif] : folder.notifications) {
                    std::thread([this, notif]() { trash(notif.path); }).detach();
                }
            }
        }
    }

    emit notificationsCached();
}

void NotificationCacher::markAsTrashed(NotificationFolder& folder) {
    for (auto& [name, notif] : folder.notifications) {
        notif.trashed = true;
        if (notif.notification_tray_run_id == m_run_id) {
            emit notificationTrashed(notif.id);
        }
    }
    for (auto& [name, subfolder] : folder.folders) {
        markAsTrashed(subfolder);
    }
}
