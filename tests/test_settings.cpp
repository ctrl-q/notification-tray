#include "utils/settings.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

class SettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for tests
        temp_dir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(temp_dir->isValid());
        root_path = fs::path(temp_dir->path().toStdString());

        Settings::resetConfigCache();

        fs::path config_path = Settings::getConfigPath();
        std::error_code ec;
        fs::remove(config_path, ec);
    }

    void TearDown() override { temp_dir.reset(); }

    void setFolderSettings(const fs::path& folder, const QJsonObject& settings) {
        QJsonObject config = Settings::loadConfig();
        QJsonObject folders = config["folders"].toObject();
        QString key = QString::fromStdString(Settings::getFolderKey(root_path, folder));
        folders[key] = settings;
        config["folders"] = folders;
        ASSERT_TRUE(Settings::writeConfig(config));
    }

    std::unique_ptr<QTemporaryDir> temp_dir;
    fs::path root_path;
};

// Tests for getNotificationBackoffMinutes

TEST_F(SettingsTest, GetNotificationBackoffMinutes_EmptyCache) {
    std::map<fs::path, int> cache;
    fs::path folder = root_path / "app" / "summary";

    int result = Settings::getNotificationBackoffMinutes(root_path, folder, cache);
    EXPECT_EQ(result, 0);
}

TEST_F(SettingsTest, GetNotificationBackoffMinutes_DirectMatch) {
    std::map<fs::path, int> cache;
    fs::path folder = root_path / "app" / "summary";
    cache[folder] = 30;

    int result = Settings::getNotificationBackoffMinutes(root_path, folder, cache);
    EXPECT_EQ(result, 30);
}

TEST_F(SettingsTest, GetNotificationBackoffMinutes_ParentMatch) {
    std::map<fs::path, int> cache;
    fs::path app_folder = root_path / "app";
    fs::path folder = root_path / "app" / "summary";
    cache[app_folder] = 15;

    int result = Settings::getNotificationBackoffMinutes(root_path, folder, cache);
    EXPECT_EQ(result, 15);
}

TEST_F(SettingsTest, GetNotificationBackoffMinutes_ClosestAncestorWins) {
    std::map<fs::path, int> cache;
    fs::path app_folder = root_path / "app";
    fs::path summary_folder = root_path / "app" / "summary";
    cache[app_folder] = 15;
    cache[summary_folder] = 30;

    int result = Settings::getNotificationBackoffMinutes(root_path, summary_folder, cache);
    EXPECT_EQ(result, 30);
}

// Tests for isDoNotDisturbActive

TEST_F(SettingsTest, IsDoNotDisturbActive_EmptyCache) {
    Cache cache;
    fs::path folder = root_path / "app";

    bool result = Settings::isDoNotDisturbActive(root_path, folder, cache);
    EXPECT_FALSE(result);
}

TEST_F(SettingsTest, IsDoNotDisturbActive_FutureTime) {
    Cache cache;
    fs::path folder = root_path / "app";
    cache[folder] = QDateTime::currentDateTimeUtc().addSecs(3600);  // 1 hour in future

    bool result = Settings::isDoNotDisturbActive(root_path, folder, cache);
    EXPECT_TRUE(result);
}

TEST_F(SettingsTest, IsDoNotDisturbActive_PastTime) {
    Cache cache;
    fs::path folder = root_path / "app";
    cache[folder] = QDateTime::currentDateTimeUtc().addSecs(-3600);  // 1 hour in past

    bool result = Settings::isDoNotDisturbActive(root_path, folder, cache);
    EXPECT_FALSE(result);
}

TEST_F(SettingsTest, IsDoNotDisturbActive_NullOptValue) {
    Cache cache;
    fs::path folder = root_path / "app";
    cache[folder] = std::nullopt;

    bool result = Settings::isDoNotDisturbActive(root_path, folder, cache);
    EXPECT_FALSE(result);
}

TEST_F(SettingsTest, IsDoNotDisturbActive_InvalidDateTime) {
    Cache cache;
    fs::path folder = root_path / "app";
    cache[folder] = QDateTime();  // Invalid datetime

    bool result = Settings::isDoNotDisturbActive(root_path, folder, cache);
    EXPECT_FALSE(result);
}

TEST_F(SettingsTest, IsDoNotDisturbActive_InheritFromParent) {
    Cache cache;
    fs::path app_folder = root_path / "app";
    fs::path folder = root_path / "app" / "summary";
    cache[app_folder] = QDateTime::currentDateTimeUtc().addSecs(3600);

    bool result = Settings::isDoNotDisturbActive(root_path, folder, cache);
    EXPECT_TRUE(result);
}

// Tests for getDoNotDisturb

TEST_F(SettingsTest, GetDoNotDisturb_EmptyCache) {
    Cache cache;
    fs::path folder = root_path / "app";

    auto result = Settings::getDoNotDisturb(root_path, folder, cache);
    EXPECT_FALSE(result.has_value());
}

TEST_F(SettingsTest, GetDoNotDisturb_DirectMatch) {
    Cache cache;
    fs::path folder = root_path / "app";
    QDateTime expected = QDateTime::currentDateTimeUtc().addSecs(3600);
    cache[folder] = expected;

    auto result = Settings::getDoNotDisturb(root_path, folder, cache);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), expected);
}

TEST_F(SettingsTest, GetDoNotDisturb_ParentMatch) {
    Cache cache;
    fs::path app_folder = root_path / "app";
    fs::path folder = root_path / "app" / "summary";
    QDateTime expected = QDateTime::currentDateTimeUtc().addSecs(7200);
    cache[app_folder] = expected;

    auto result = Settings::getDoNotDisturb(root_path, folder, cache);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), expected);
}

// Tests for isHideFromTrayActive

TEST_F(SettingsTest, IsHideFromTrayActive_FutureTime) {
    Cache cache;
    fs::path folder = root_path / "app";
    cache[folder] = QDateTime::currentDateTimeUtc().addSecs(3600);

    bool result = Settings::isHideFromTrayActive(root_path, folder, cache);
    EXPECT_TRUE(result);
}

TEST_F(SettingsTest, IsHideFromTrayActive_PastTime) {
    Cache cache;
    fs::path folder = root_path / "app";
    cache[folder] = QDateTime::currentDateTimeUtc().addSecs(-3600);

    bool result = Settings::isHideFromTrayActive(root_path, folder, cache);
    EXPECT_FALSE(result);
}

// Tests for cacheDateTimeSetting

TEST_F(SettingsTest, CacheDateTimeSetting_NoSettingsFile) {
    Cache cache;
    fs::path folder = root_path / "app";
    fs::create_directories(folder);

    Settings::cacheDateTimeSetting(root_path, folder, "do_not_disturb_until", cache);
    EXPECT_TRUE(cache.empty());
}

TEST_F(SettingsTest, CacheDateTimeSetting_SettingExists) {
    Cache cache;
    fs::path folder = root_path / "app";

    QDateTime expected = QDateTime::currentDateTimeUtc().addSecs(3600);
    QJsonObject settings;
    settings["do_not_disturb_until"] = expected.toString(Qt::ISODate);
    setFolderSettings(folder, settings);

    Settings::cacheDateTimeSetting(root_path, folder, "do_not_disturb_until", cache);

    ASSERT_TRUE(cache.count(folder) > 0);
    ASSERT_TRUE(cache[folder].has_value());
    // Allow 1 second tolerance for datetime comparison
    EXPECT_LE(qAbs(cache[folder].value().secsTo(expected)), 1);
}

TEST_F(SettingsTest, CacheDateTimeSetting_EmptyString) {
    Cache cache;
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["do_not_disturb_until"] = "";
    setFolderSettings(folder, settings);

    Settings::cacheDateTimeSetting(root_path, folder, "do_not_disturb_until", cache);

    ASSERT_TRUE(cache.count(folder) > 0);
    EXPECT_FALSE(cache[folder].has_value());
}

TEST_F(SettingsTest, CacheDateTimeSetting_SettingMissing) {
    Cache cache;
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["other_setting"] = "value";
    setFolderSettings(folder, settings);

    Settings::cacheDateTimeSetting(root_path, folder, "do_not_disturb_until", cache);
    EXPECT_TRUE(cache.empty());
}

// Tests for writeDateTimeSetting

TEST_F(SettingsTest, WriteDateTimeSetting_CreatesFile) {
    Cache cache;
    fs::path folder = root_path / "app";
    fs::create_directories(folder);

    QDateTime until = QDateTime::currentDateTimeUtc().addSecs(3600);
    Settings::writeDateTimeSetting(root_path, folder, "do_not_disturb_until", until, cache);

    // Check cache was updated
    ASSERT_TRUE(cache.count(folder) > 0);
    ASSERT_TRUE(cache[folder].has_value());
    EXPECT_EQ(cache[folder].value(), until);

    // Check config file was created
    fs::path settings_file = Settings::getConfigPath();
    EXPECT_TRUE(fs::exists(settings_file));

    // Verify file contents in folder section
    QFile file(QString::fromStdString(settings_file.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    ASSERT_TRUE(doc.isObject());
    QJsonObject folders = doc.object()["folders"].toObject();
    QString key = QString::fromStdString(Settings::getFolderKey(root_path, folder));
    QJsonObject folder_obj = folders[key].toObject();
    EXPECT_TRUE(folder_obj.contains("do_not_disturb_until"));
    EXPECT_EQ(folder_obj["do_not_disturb_until"].toString(), until.toString(Qt::ISODate));
}

TEST_F(SettingsTest, WriteDateTimeSetting_PreservesExistingSettings) {
    Cache cache;
    fs::path folder = root_path / "app";

    // Create file with existing settings
    QJsonObject existing;
    existing["other_setting"] = "existing_value";
    existing["notification_backoff_minutes"] = 30;
    setFolderSettings(folder, existing);

    QDateTime until = QDateTime::currentDateTimeUtc().addSecs(3600);
    Settings::writeDateTimeSetting(root_path, folder, "do_not_disturb_until", until, cache);

    // Verify both old and new settings exist in folder section
    fs::path settings_file = Settings::getConfigPath();
    QFile file(QString::fromStdString(settings_file.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject folders = doc.object()["folders"].toObject();
    QString key = QString::fromStdString(Settings::getFolderKey(root_path, folder));
    QJsonObject obj = folders[key].toObject();

    EXPECT_EQ(obj["other_setting"].toString(), "existing_value");
    EXPECT_EQ(obj["notification_backoff_minutes"].toInt(), 30);
    EXPECT_TRUE(obj.contains("do_not_disturb_until"));
}

TEST_F(SettingsTest, WriteDateTimeSetting_OverwritesExistingValue) {
    Cache cache;
    fs::path folder = root_path / "app";

    // Create file with existing DnD setting
    QDateTime old_time = QDateTime::currentDateTimeUtc().addSecs(-3600);
    QJsonObject existing;
    existing["do_not_disturb_until"] = old_time.toString(Qt::ISODate);
    setFolderSettings(folder, existing);

    // Write new value
    QDateTime new_time = QDateTime::currentDateTimeUtc().addSecs(7200);
    Settings::writeDateTimeSetting(root_path, folder, "do_not_disturb_until", new_time, cache);

    // Verify new value in folder section
    fs::path settings_file = Settings::getConfigPath();
    QFile file(QString::fromStdString(settings_file.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject folders = doc.object()["folders"].toObject();
    QString key = QString::fromStdString(Settings::getFolderKey(root_path, folder));
    QJsonObject obj = folders[key].toObject();

    EXPECT_EQ(obj["do_not_disturb_until"].toString(), new_time.toString(Qt::ISODate));
}

// Tests for getSoundFile

TEST_F(SettingsTest, GetSoundFile_NoSettingsFile) {
    fs::path folder = root_path / "app" / "summary";
    fs::create_directories(folder);

    std::string result = Settings::getSoundFile(root_path, folder);
    EXPECT_TRUE(result.empty());
}

TEST_F(SettingsTest, GetSoundFile_AbsolutePath) {
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["sound"] = "/usr/share/sounds/alert.wav";
    setFolderSettings(folder, settings);

    std::string result = Settings::getSoundFile(root_path, folder);
    EXPECT_EQ(result, "/usr/share/sounds/alert.wav");
}

TEST_F(SettingsTest, GetSoundFile_RelativePathResolvedToSettingsDir) {
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["sound"] = "alert.wav";
    setFolderSettings(folder, settings);

    std::string result = Settings::getSoundFile(root_path, folder);
    EXPECT_EQ(result, (folder / "alert.wav").string());
}

TEST_F(SettingsTest, GetSoundFile_InheritedFromParent) {
    fs::path app_folder = root_path / "app";
    fs::path summary_folder = root_path / "app" / "summary";
    fs::create_directories(summary_folder);

    QJsonObject settings;
    settings["sound"] = "/sounds/notify.wav";
    setFolderSettings(app_folder, settings);

    std::string result = Settings::getSoundFile(root_path, summary_folder);
    EXPECT_EQ(result, "/sounds/notify.wav");
}

TEST_F(SettingsTest, GetSoundFile_ChildOverridesParent) {
    fs::path app_folder = root_path / "app";
    fs::path summary_folder = root_path / "app" / "summary";

    QJsonObject parent_settings;
    parent_settings["sound"] = "/sounds/parent.wav";
    setFolderSettings(app_folder, parent_settings);

    QJsonObject child_settings;
    child_settings["sound"] = "/sounds/child.wav";
    setFolderSettings(summary_folder, child_settings);

    std::string result = Settings::getSoundFile(root_path, summary_folder);
    EXPECT_EQ(result, "/sounds/child.wav");
}

TEST_F(SettingsTest, GetSoundFile_EmptyStringReturnsEmpty) {
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["sound"] = "";
    setFolderSettings(folder, settings);

    std::string result = Settings::getSoundFile(root_path, folder);
    EXPECT_TRUE(result.empty());
}

TEST_F(SettingsTest, GetSoundFile_MissingKeyReturnsEmpty) {
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["other_setting"] = "value";
    setFolderSettings(folder, settings);

    std::string result = Settings::getSoundFile(root_path, folder);
    EXPECT_TRUE(result.empty());
}

TEST_F(SettingsTest, FolderKey_RootIsDot) {
    EXPECT_EQ(Settings::getFolderKey(root_path, root_path), ".");
}

TEST_F(SettingsTest, FolderKey_NestedUsesForwardSlashes) {
    fs::path folder = root_path / "firefox" / "new-tab";
    EXPECT_EQ(Settings::getFolderKey(root_path, folder), "firefox/new-tab");
}

TEST_F(SettingsTest, FolderPathFromKey_DotResolvesToRoot) {
    EXPECT_EQ(Settings::getFolderPathFromKey(root_path, "."), root_path);
}

TEST_F(SettingsTest, FolderPathFromKey_NestedResolvesUnderRoot) {
    fs::path expected = root_path / "firefox" / "new-tab";
    EXPECT_EQ(Settings::getFolderPathFromKey(root_path, "firefox/new-tab"), expected);
}

TEST_F(SettingsTest, HasFolderSettingAtOrBelow_ReturnsTrueForExactMatch) {
    fs::path folder = root_path / "firefox";
    setFolderSettings(folder, QJsonObject{{"sound", "notify.wav"}});

    EXPECT_TRUE(Settings::hasFolderSettingAtOrBelow(root_path, folder));
}

TEST_F(SettingsTest, HasFolderSettingAtOrBelow_ReturnsTrueForDescendantMatch) {
    fs::path parent = root_path / "firefox";
    fs::path child = root_path / "firefox" / "new-tab";
    setFolderSettings(child, QJsonObject{{"sound", "notify.wav"}});

    EXPECT_TRUE(Settings::hasFolderSettingAtOrBelow(root_path, parent));
}

TEST_F(SettingsTest, HasFolderSettingAtOrBelow_ReturnsFalseWithoutMatch) {
    fs::path folder = root_path / "firefox";
    setFolderSettings(root_path / "discord", QJsonObject{{"sound", "notify.wav"}});

    EXPECT_FALSE(Settings::hasFolderSettingAtOrBelow(root_path, folder));
}
