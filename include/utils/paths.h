#pragma once

#include "../notification_types.h"

#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

class Paths {
public:
    static fs::path getOutputPath(const fs::path& root_path, const Notification& notification);

    static QString slugify(const QString& text);

private:
    static std::optional<QStringList> evaluateSubdirCallback(const QString& callback_code,
                                                             const Notification& notification);

    static fs::path getCustomOutputDir(const fs::path& root_path, const fs::path& default_outdir,
                                       const Notification& notification);
};
