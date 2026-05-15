#include "utils/settings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QJsonObject getFolderSection(const QJsonObject& folders, const std::string& key) {
    return folders.value(QString::fromStdString(key)).toObject();
}
}  // namespace

fs::path Settings::getConfigPath() {
    QString config_dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return fs::path((config_dir + "/notification-tray/settings.json").toStdString());
}

QJsonObject Settings::loadConfig() {
    QJsonObject config;
    config["version"] = 1;
    config["folders"] = QJsonObject();

    fs::path config_path = getConfigPath();
    QFile file(QString::fromStdString(config_path.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return config;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return config;
    }

    QJsonObject loaded = doc.object();
    if (!loaded.contains("version")) {
        loaded["version"] = 1;
    }
    if (!loaded.contains("folders") || !loaded["folders"].isObject()) {
        loaded["folders"] = QJsonObject();
    }
    return loaded;
}

bool Settings::writeConfig(const QJsonObject& config) {
    fs::path config_path = getConfigPath();
    QDir dir;
    if (!dir.mkpath(QString::fromStdString(config_path.parent_path().string()))) {
        return false;
    }

    QSaveFile file(QString::fromStdString(config_path.string()));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(config).toJson());
    if (!file.commit()) {
        return false;
    }
    return true;
}

void Settings::resetConfigCache() {}

std::string Settings::getFolderKey(const fs::path& root_path, const fs::path& folder_path) {
    fs::path relative = folder_path.lexically_normal().lexically_relative(root_path.lexically_normal());
    if (relative.empty() || relative == ".") {
        return ".";
    }
    return relative.generic_string();
}

fs::path Settings::getFolderPathFromKey(const fs::path& root_path, const std::string& key) {
    if (key == ".") {
        return root_path;
    }
    return (root_path / fs::path(key)).lexically_normal();
}

bool Settings::hasFolderSettingAtOrBelow(const fs::path& root_path, const fs::path& folder_path) {
    QJsonObject folders = loadConfig().value("folders").toObject();

    fs::path normalized_folder = folder_path.lexically_normal();
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        fs::path candidate = getFolderPathFromKey(root_path, it.key().toStdString());
        fs::path relative = candidate.lexically_relative(normalized_folder);
        if (relative.empty() || relative == "." ||
            (!relative.empty() && *relative.begin() != "..")) {
            return true;
        }
    }
    return false;
}

int Settings::getNotificationBackoffMinutes(const fs::path& root_path, const fs::path& folder_path,
                                             const std::map<fs::path, int>& cache) {
    for (auto p = folder_path; p != root_path.parent_path(); p = p.parent_path()) {
        auto it = cache.find(p);
        if (it != cache.end()) {
            return it->second;
        }
    }
    return 0;
}

std::string Settings::getSoundFile(const fs::path& root_path, const fs::path& folder_path) {
    QJsonObject folders = loadConfig().value("folders").toObject();

    for (auto p = folder_path; p != root_path.parent_path(); p = p.parent_path()) {
        std::string key = getFolderKey(root_path, p);
        QJsonObject section = getFolderSection(folders, key);
        if (!section.contains("sound")) {
            continue;
        }

        QString sound = section["sound"].toString();
        if (sound.isEmpty()) {
            continue;
        }

        fs::path sound_path(sound.toStdString());
        if (sound_path.is_relative()) {
            sound_path = getFolderPathFromKey(root_path, key) / sound_path;
        }

        return sound_path.string();
    }
    return {};
}

bool Settings::isDoNotDisturbActive(const fs::path& root_path, const fs::path& folder_path,
                                    const Cache& cache) {
    return isDateTimeSettingActive(folder_path, root_path, cache);
}

std::optional<QDateTime> Settings::getDoNotDisturb(const fs::path& root_path,
                                                   const fs::path& folder_path,
                                                   const Cache& cache) {
    return getDateTimeSetting(folder_path, root_path, cache);
}

bool Settings::isHideFromTrayActive(const fs::path& root_path, const fs::path& folder_path,
                                    const Cache& cache) {
    return isDateTimeSettingActive(folder_path, root_path, cache);
}

void Settings::cacheDateTimeSetting(const fs::path& root_path, const fs::path& folder_path,
                                    const QString& setting_name, Cache& cache) {
    QJsonObject folders = loadConfig().value("folders").toObject();
    std::string key = getFolderKey(root_path, folder_path);
    QJsonObject section = getFolderSection(folders, key);
    if (!section.contains(setting_name)) {
        return;
    }

    QString dateStr = section[setting_name].toString();
    if (!dateStr.isEmpty()) {
        cache[folder_path] = QDateTime::fromString(dateStr, Qt::ISODate);
    } else {
        cache[folder_path] = std::nullopt;
    }
}

void Settings::writeDateTimeSetting(const fs::path& root_path, const fs::path& folder_path,
                                    const QString& setting_name, const QDateTime& until,
                                    Cache& cache) {
    cache[folder_path] = until;
    writeFolderSetting(root_path, folder_path, setting_name, until.toString(Qt::ISODate));
}

void Settings::writeIntSetting(const fs::path& root_path, const fs::path& folder_path,
                               const QString& setting_name, int value,
                               std::map<fs::path, int>& cache) {
    cache[folder_path] = value;
    writeFolderSetting(root_path, folder_path, setting_name, value);
}

bool Settings::writeFolderSetting(const fs::path& root_path, const fs::path& folder_path,
                                  const QString& setting_name, const QJsonValue& value) {
    QJsonObject config = loadConfig();
    QJsonObject folders = config.value("folders").toObject();

    QString key = QString::fromStdString(getFolderKey(root_path, folder_path));
    QJsonObject section = folders.value(key).toObject();
    section[setting_name] = value;
    folders[key] = section;

    config["folders"] = folders;
    return writeConfig(config);
}

std::optional<QDateTime> Settings::getDateTimeSetting(const fs::path& folder_path,
                                                      const fs::path& root_path,
                                                      const Cache& cache) {
    for (auto p = folder_path; p != root_path.parent_path(); p = p.parent_path()) {
        auto it = cache.find(p);
        if (it != cache.end()) {
            return it->second;
        }
    }
    return std::nullopt;
}

bool Settings::isDateTimeSettingActive(const fs::path& folder_path, const fs::path& root_path,
                                       const Cache& cache) {
    auto datetime = getDateTimeSetting(folder_path, root_path, cache);
    if (!datetime.has_value() || !datetime.value().isValid()) {
        return false;
    }
    return datetime.value() > QDateTime::currentDateTimeUtc();
}
