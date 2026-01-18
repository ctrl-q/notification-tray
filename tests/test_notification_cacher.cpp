#include "notification_cacher.h"
#include "notifier.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class NotificationCacherTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(temp_dir->isValid());
        root_path = fs::path(temp_dir->path().toStdString());

        // Initialize shared state
        do_not_disturb.clear();
        notification_backoff_minutes.clear();
        notification_cache.path = root_path;
        notification_cache.folders.clear();
        notification_cache.notifications.clear();

        // Create the cacher (notifier is nullptr for unit tests)
        cacher = std::make_unique<NotificationCacher>(root_path, nullptr, do_not_disturb,
                                                      notification_backoff_minutes,
                                                      notification_cache, run_id);
    }

    void TearDown() override {
        cacher.reset();
        temp_dir.reset();
    }

    CachedNotification createTestNotification(const QString& app_name, const QString& summary,
                                              int id, const fs::path& path) {
        CachedNotification n;
        n.app_name = app_name;
        n.summary = summary;
        n.body = "Test body";
        n.app_icon = "test-icon";
        n.id = id;
        n.replaces_id = 0;
        n.expire_timeout = -1;
        n.notification_tray_run_id = run_id;
        n.at = QDateTime::currentDateTimeUtc();
        n.path = path;
        n.trashed = false;
        return n;
    }

    void writeNotificationFile(const fs::path& path, const CachedNotification& n) {
        fs::create_directories(path.parent_path());

        QJsonObject obj;
        obj["app_name"] = n.app_name;
        obj["summary"] = n.summary;
        obj["body"] = n.body;
        obj["app_icon"] = n.app_icon;
        obj["id"] = n.id;
        obj["replaces_id"] = n.replaces_id;
        obj["expire_timeout"] = n.expire_timeout;
        obj["notification_tray_run_id"] = n.notification_tray_run_id;
        obj["actions"] = QJsonObject();
        obj["hints"] = QJsonObject();

        QFile file(QString::fromStdString(path.string()));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(obj).toJson());
        file.close();
    }

    std::unique_ptr<QTemporaryDir> temp_dir;
    fs::path root_path;
    Cache do_not_disturb;
    std::map<fs::path, int> notification_backoff_minutes;
    NotificationFolder notification_cache;
    QString run_id = "test-run-id";
    std::unique_ptr<NotificationCacher> cacher;
};

// Tests for cache()

TEST_F(NotificationCacherTest, Cache_CreatesFile) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);

    cacher->cache(n);

    EXPECT_TRUE(fs::exists(notif_path));
}

TEST_F(NotificationCacherTest, Cache_WritesCorrectContent) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);

    cacher->cache(n);

    QFile file(QString::fromStdString(notif_path.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    EXPECT_EQ(obj["app_name"].toString(), "Firefox");
    EXPECT_EQ(obj["summary"].toString(), "New Tab");
    EXPECT_EQ(obj["id"].toInt(), 1);
}

TEST_F(NotificationCacherTest, Cache_UpdatesInMemoryCache) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);

    cacher->cache(n);

    // Check that notification was added to cache
    EXPECT_TRUE(notification_cache.folders.count("firefox") > 0);
    auto& app_folder = notification_cache.folders["firefox"];
    EXPECT_TRUE(app_folder.folders.count("new-tab") > 0);
}

TEST_F(NotificationCacherTest, Cache_EmitsSignal) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);

    QSignalSpy spy(cacher.get(), &NotificationCacher::notificationsCached);
    ASSERT_TRUE(spy.isValid());

    cacher->cache(n);

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(NotificationCacherTest, Cache_TransientNotWritten) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    n.hints["transient"] = true;

    cacher->cache(n);

    // File should not be created for transient notifications
    EXPECT_FALSE(fs::exists(notif_path));
    // But should still be in memory cache
    EXPECT_TRUE(notification_cache.folders.count("firefox") > 0);
}

TEST_F(NotificationCacherTest, Cache_WithActions) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    n.actions["default"] = "Open";
    n.actions["dismiss"] = "Dismiss";

    cacher->cache(n);

    QFile file(QString::fromStdString(notif_path.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();
    QJsonObject actions = obj["actions"].toObject();

    EXPECT_EQ(actions["default"].toString(), "Open");
    EXPECT_EQ(actions["dismiss"].toString(), "Dismiss");
}

TEST_F(NotificationCacherTest, Cache_WithHints) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "test-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    n.hints["urgency"] = 2;
    n.hints["category"] = "email.arrived";

    cacher->cache(n);

    QFile file(QString::fromStdString(notif_path.string()));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();
    QJsonObject hints = obj["hints"].toObject();

    EXPECT_EQ(hints["urgency"].toInt(), 2);
    EXPECT_EQ(hints["category"].toString(), "email.arrived");
}

// Tests for cacheExistingNotifications()

TEST_F(NotificationCacherTest, CacheExisting_EmptyDirectory) {
    QSignalSpy spy(cacher.get(), &NotificationCacher::notificationsCached);
    cacher->cacheExistingNotifications(root_path);

    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(notification_cache.folders.empty());
}

TEST_F(NotificationCacherTest, CacheExisting_LoadsSingleFile) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "run-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    writeNotificationFile(notif_path, n);

    cacher->cacheExistingNotifications(root_path);

    EXPECT_TRUE(notification_cache.folders.count("firefox") > 0);
}

TEST_F(NotificationCacherTest, CacheExisting_IgnoresSettingsFile) {
    // Create a .settings.json file
    fs::path settings_path = root_path / "firefox" / ".settings.json";
    fs::create_directories(settings_path.parent_path());
    QFile file(QString::fromStdString(settings_path.string()));
    file.open(QIODevice::WriteOnly);
    file.write("{}");
    file.close();

    cacher->cacheExistingNotifications(root_path);

    // Settings file should not be treated as notification
    if (notification_cache.folders.count("firefox") > 0) {
        EXPECT_TRUE(notification_cache.folders["firefox"].notifications.empty());
    }
}

TEST_F(NotificationCacherTest, CacheExisting_LoadsMultipleFiles) {
    for (int i = 1; i <= 5; ++i) {
        fs::path notif_path =
            root_path / "firefox" / "new-tab" / (QString("run-%1.json").arg(i).toStdString());
        CachedNotification n = createTestNotification("Firefox", "New Tab", i, notif_path);
        writeNotificationFile(notif_path, n);
    }

    cacher->cacheExistingNotifications(root_path);

    EXPECT_EQ(notification_cache.folders["firefox"].folders["new-tab"].notifications.size(), 5u);
}

TEST_F(NotificationCacherTest, CacheExisting_LoadsMultipleApps) {
    fs::path path1 = root_path / "firefox" / "tab" / "run-1.json";
    fs::path path2 = root_path / "chrome" / "tab" / "run-1.json";
    fs::path path3 = root_path / "slack" / "message" / "run-1.json";

    writeNotificationFile(path1, createTestNotification("Firefox", "Tab", 1, path1));
    writeNotificationFile(path2, createTestNotification("Chrome", "Tab", 2, path2));
    writeNotificationFile(path3, createTestNotification("Slack", "Message", 3, path3));

    cacher->cacheExistingNotifications(root_path);

    EXPECT_TRUE(notification_cache.folders.count("firefox") > 0);
    EXPECT_TRUE(notification_cache.folders.count("chrome") > 0);
    EXPECT_TRUE(notification_cache.folders.count("slack") > 0);
}

// Tests for trash()

TEST_F(NotificationCacherTest, Trash_NonExistentPath) {
    fs::path fake_path = root_path / "nonexistent.json";

    // Should not crash
    cacher->trash(fake_path);
}

TEST_F(NotificationCacherTest, Trash_SingleFile) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "run-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    cacher->cache(n);

    ASSERT_TRUE(fs::exists(notif_path));

    cacher->trash(notif_path);

    // File should no longer exist
    EXPECT_FALSE(fs::exists(notif_path));
}

TEST_F(NotificationCacherTest, Trash_MarksCacheAsTrashed) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "run-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    cacher->cache(n);

    cacher->trash(notif_path);

    // Check in-memory cache is marked as trashed
    auto& notifications = notification_cache.folders["firefox"].folders["new-tab"].notifications;
    if (!notifications.empty()) {
        EXPECT_TRUE(notifications.begin()->second.trashed);
    }
}

TEST_F(NotificationCacherTest, Trash_EmitsSignalForThisRun) {
    fs::path notif_path = root_path / "firefox" / "new-tab" / "run-1.json";
    CachedNotification n = createTestNotification("Firefox", "New Tab", 1, notif_path);
    cacher->cache(n);

    QSignalSpy spy(cacher.get(), &NotificationCacher::notificationTrashed);
    ASSERT_TRUE(spy.isValid());

    cacher->trash(notif_path);

    // Signal should be emitted because notification is from this run
    EXPECT_GE(spy.count(), 1);
}
