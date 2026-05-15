#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QJsonValue>

#include <filesystem>
#include <map>
#include <optional>

namespace fs = std::filesystem;

using Cache = std::map<fs::path, std::optional<QDateTime>>;

class Settings {
public:
    static fs::path getConfigPath();
    static QJsonObject loadConfig();
    static bool writeConfig(const QJsonObject& config);
    static void resetConfigCache();

    static std::string getFolderKey(const fs::path& root_path, const fs::path& folder_path);
    static fs::path getFolderPathFromKey(const fs::path& root_path, const std::string& key);

    static bool hasFolderSettingAtOrBelow(const fs::path& root_path, const fs::path& folder_path);

    static int getNotificationBackoffMinutes(const fs::path& root_path, const fs::path& folder_path,
                                             const std::map<fs::path, int>& cache);

    static std::string getSoundFile(const fs::path& root_path, const fs::path& folder_path);

    static bool isDoNotDisturbActive(const fs::path& root_path, const fs::path& folder_path,
                                     const Cache& cache);

    static std::optional<QDateTime>
    getDoNotDisturb(const fs::path& root_path, const fs::path& folder_path, const Cache& cache);

    static bool isHideFromTrayActive(const fs::path& root_path, const fs::path& folder_path,
                                     const Cache& cache);

    static void cacheDateTimeSetting(const fs::path& root_path, const fs::path& folder_path,
                                     const QString& setting_name, Cache& cache);

    static void writeDateTimeSetting(const fs::path& root_path, const fs::path& folder_path,
                                     const QString& setting_name, const QDateTime& until,
                                     Cache& cache);

    static void writeIntSetting(const fs::path& root_path, const fs::path& folder_path,
                                const QString& setting_name, int value,
                                std::map<fs::path, int>& cache);

private:
    static bool writeFolderSetting(const fs::path& root_path, const fs::path& folder_path,
                                   const QString& setting_name, const QJsonValue& value);

    static std::optional<QDateTime>
    getDateTimeSetting(const fs::path& folder_path, const fs::path& root_path, const Cache& cache);

    static bool isDateTimeSettingActive(const fs::path& folder_path, const fs::path& root_path,
                                        const Cache& cache);
};
