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
    }

    void TearDown() override { temp_dir.reset(); }

    void createSettingsFile(const fs::path& folder, const QJsonObject& settings) {
        fs::create_directories(folder);
        fs::path settings_file = folder / ".settings.json";
        QFile file(QString::fromStdString(settings_file.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(settings).toJson());
        file.close();
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

    Settings::cacheDateTimeSetting(folder, "do_not_disturb_until", cache);
    EXPECT_TRUE(cache.empty());
}

TEST_F(SettingsTest, CacheDateTimeSetting_SettingExists) {
    Cache cache;
    fs::path folder = root_path / "app";

    QDateTime expected = QDateTime::currentDateTimeUtc().addSecs(3600);
    QJsonObject settings;
    settings["do_not_disturb_until"] = expected.toString(Qt::ISODate);
    createSettingsFile(folder, settings);

    Settings::cacheDateTimeSetting(folder, "do_not_disturb_until", cache);

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
    createSettingsFile(folder, settings);

    Settings::cacheDateTimeSetting(folder, "do_not_disturb_until", cache);

    ASSERT_TRUE(cache.count(folder) > 0);
    EXPECT_FALSE(cache[folder].has_value());
}

TEST_F(SettingsTest, CacheDateTimeSetting_SettingMissing) {
    Cache cache;
    fs::path folder = root_path / "app";

    QJsonObject settings;
    settings["other_setting"] = "value";
    createSettingsFile(folder, settings);

    Settings::cacheDateTimeSetting(folder, "do_not_disturb_until", cache);
    EXPECT_TRUE(cache.empty());
}

// Tests for writeDateTimeSetting

TEST_F(SettingsTest, WriteDateTimeSetting_CreatesFile) {
    Cache cache;
    fs::path folder = root_path / "app";
    fs::create_directories(folder);

    QDateTime until = QDateTime::currentDateTimeUtc().addSecs(3600);
    Settings::writeDateTimeSetting(folder, "do_not_disturb_until", until, cache);

    // Check cache was updated
    ASSERT_TRUE(cache.count(folder) > 0);
    ASSERT_TRUE(cache[folder].has_value());
    EXPECT_EQ(cache[folder].value(), until);

    // Check file was created
    fs::path settings_file = folder / ".settings.json";
    EXPECT_TRUE(fs::exists(settings_file));

    // Verify file contents
    QFile file(QString::fromStdString(settings_file.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    ASSERT_TRUE(doc.isObject());
    QJsonObject obj = doc.object();
    EXPECT_TRUE(obj.contains("do_not_disturb_until"));
    EXPECT_EQ(obj["do_not_disturb_until"].toString(), until.toString(Qt::ISODate));
}

TEST_F(SettingsTest, WriteDateTimeSetting_PreservesExistingSettings) {
    Cache cache;
    fs::path folder = root_path / "app";

    // Create file with existing settings
    QJsonObject existing;
    existing["other_setting"] = "existing_value";
    existing["notification_backoff_minutes"] = 30;
    createSettingsFile(folder, existing);

    QDateTime until = QDateTime::currentDateTimeUtc().addSecs(3600);
    Settings::writeDateTimeSetting(folder, "do_not_disturb_until", until, cache);

    // Verify both old and new settings exist
    fs::path settings_file = folder / ".settings.json";
    QFile file(QString::fromStdString(settings_file.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

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
    createSettingsFile(folder, existing);

    // Write new value
    QDateTime new_time = QDateTime::currentDateTimeUtc().addSecs(7200);
    Settings::writeDateTimeSetting(folder, "do_not_disturb_until", new_time, cache);

    // Verify new value
    fs::path settings_file = folder / ".settings.json";
    QFile file(QString::fromStdString(settings_file.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    EXPECT_EQ(obj["do_not_disturb_until"].toString(), new_time.toString(Qt::ISODate));
}
