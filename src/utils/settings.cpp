#include "utils/settings.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

int Settings::getNotificationBackoffMinutes(const fs::path& root_path, const fs::path& folder_path,
                                            const std::map<fs::path, int>& cache) {
    auto relative = std::filesystem::relative(folder_path, root_path);

    for (auto p = folder_path; p != root_path.parent_path(); p = p.parent_path()) {
        auto it = cache.find(p);
        if (it != cache.end()) {
            return it->second;
        }
    }
    return 0;
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

void Settings::cacheDateTimeSetting(const fs::path& folder_path, const QString& setting_name,
                                    Cache& cache) {
    fs::path settings_file = folder_path / ".settings.json";

    QFile file(QString::fromStdString(settings_file.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    QJsonObject obj = doc.object();
    if (obj.contains(setting_name)) {
        QString dateStr = obj[setting_name].toString();
        if (!dateStr.isEmpty()) {
            cache[folder_path] = QDateTime::fromString(dateStr, Qt::ISODate);
        } else {
            cache[folder_path] = std::nullopt;
        }
    }
}

void Settings::writeDateTimeSetting(const fs::path& folder_path, const QString& setting_name,
                                    const QDateTime& until, Cache& cache) {
    cache[folder_path] = until;

    fs::path settings_file = folder_path / ".settings.json";
    QString settings_path = QString::fromStdString(settings_file.string());

    QJsonObject existing_settings;
    QFile file(settings_path);
    if (file.open(QIODevice::ReadOnly)) {
        existing_settings = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    existing_settings[setting_name] = until.toString(Qt::ISODate);

    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(existing_settings).toJson());
        file.close();
    }
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
