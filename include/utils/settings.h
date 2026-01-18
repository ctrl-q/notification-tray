#pragma once

#include <QDateTime>

#include <filesystem>
#include <map>
#include <optional>

namespace fs = std::filesystem;

using Cache = std::map<fs::path, std::optional<QDateTime>>;

class Settings {
public:
    static int getNotificationBackoffMinutes(const fs::path& root_path, const fs::path& folder_path,
                                             const std::map<fs::path, int>& cache);

    static bool isDoNotDisturbActive(const fs::path& root_path, const fs::path& folder_path,
                                     const Cache& cache);

    static std::optional<QDateTime>
    getDoNotDisturb(const fs::path& root_path, const fs::path& folder_path, const Cache& cache);

    static bool isHideFromTrayActive(const fs::path& root_path, const fs::path& folder_path,
                                     const Cache& cache);

    static void cacheDateTimeSetting(const fs::path& folder_path, const QString& setting_name,
                                     Cache& cache);

    static void writeDateTimeSetting(const fs::path& folder_path, const QString& setting_name,
                                     const QDateTime& until, Cache& cache);

private:
    static std::optional<QDateTime>
    getDateTimeSetting(const fs::path& folder_path, const fs::path& root_path, const Cache& cache);

    static bool isDateTimeSettingActive(const fs::path& folder_path, const fs::path& root_path,
                                        const Cache& cache);
};
