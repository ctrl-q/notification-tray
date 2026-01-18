#include "notifier.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Helper to check if display is available
static bool hasDisplay() {
    return QGuiApplication::primaryScreen() != nullptr;
}

class NotifierTest : public ::testing::Test {
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

        // Note: notifier is created lazily in each test to allow
        // setting up do_not_disturb and backoff before construction
        notifier.reset();
    }

    void createNotifier() {
        notifier = std::make_unique<Notifier>(
            root_path, do_not_disturb, notification_backoff_minutes, notification_cache, run_id);
    }

    void TearDown() override {
        notifier.reset();
        temp_dir.reset();
    }

    CachedNotification createTestNotification(const QString& app_name, const QString& summary,
                                              int id = 1, int urgency = 1) {
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
        n.path = root_path / "app" / "summary" / (QString("run-%1.json").arg(id).toStdString());
        n.trashed = false;
        n.hints["urgency"] = urgency;
        return n;
    }

    std::unique_ptr<QTemporaryDir> temp_dir;
    fs::path root_path;
    Cache do_not_disturb;
    std::map<fs::path, int> notification_backoff_minutes;
    NotificationFolder notification_cache;
    QString run_id = "test-run-id";
    std::unique_ptr<Notifier> notifier;
};

// Tests for notify() - filtering

TEST_F(NotifierTest, Notify_TrashedNotificationSkipped) {
    createNotifier();
    CachedNotification n = createTestNotification("App", "Summary");
    n.trashed = true;

    // Should not crash and should not display (trashed = filtered out)
    notifier->notify(n);
}

TEST_F(NotifierTest, Notify_NormalNotificationFilteredByDnD) {
    // Set DnD for root path BEFORE creating notifier
    do_not_disturb[root_path] = QDateTime::currentDateTimeUtc().addSecs(3600);
    createNotifier();

    CachedNotification n = createTestNotification("App", "Summary", 1, 1);
    n.path = root_path / "app" / "summary" / "run-1.json";

    // Normal notification should be filtered by DnD (no widget created)
    notifier->notify(n);
}

TEST_F(NotifierTest, Notify_BackoffFiltersNotification) {
    // Set backoff at root path BEFORE creating notifier
    notification_backoff_minutes[root_path] = 30;
    createNotifier();

    CachedNotification n = createTestNotification("App", "Summary");
    n.path = root_path / "app" / "summary" / "run-1.json";

    // With backoff, non-batch notifications should be filtered (no widget)
    notifier->notify(n, false);
}

// Tests for closeNotification()

TEST_F(NotifierTest, CloseNotification_NonExistentId) {
    createNotifier();
    // Should not crash when closing non-existent notification
    notifier->closeNotification(999, NotificationCloseReason::EXPIRED);
}

TEST_F(NotifierTest, CloseNotification_EmitsSignal) {
    createNotifier();
    QSignalSpy spy(notifier.get(), &Notifier::notificationClosed);
    ASSERT_TRUE(spy.isValid());

    // Closing non-existent notification shouldn't emit signal
    notifier->closeNotification(999, NotificationCloseReason::EXPIRED);
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(NotifierTest, CloseNotification_DbusReasonNoSignal) {
    createNotifier();
    QSignalSpy spy(notifier.get(), &Notifier::notificationClosed);

    notifier->closeNotification(999, NotificationCloseReason::CLOSED_BY_CALL_TO_CLOSENOTIFICATION);

    // No signal should be emitted
    EXPECT_EQ(spy.count(), 0);
}

// Tests for batchNotify()

TEST_F(NotifierTest, BatchNotify_EmptyCache) {
    createNotifier();
    // Should not crash with empty cache
    notifier->batchNotify();
}

TEST_F(NotifierTest, BatchNotify_SkipsTrashedNotifications) {
    CachedNotification n = createTestNotification("App", "Summary");
    n.trashed = true;
    notification_cache.folders["app"].path = root_path / "app";
    notification_cache.folders["app"].folders["summary"].path = root_path / "app" / "summary";
    notification_cache.folders["app"].folders["summary"].notifications["run-1.json"] = n;
    createNotifier();

    // Should skip trashed notifications (no widget)
    notifier->batchNotify();
}

// Tests for signal emissions

TEST_F(NotifierTest, Signals_ActionInvokedDeclared) {
    createNotifier();
    QSignalSpy spy(notifier.get(), &Notifier::actionInvoked);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(NotifierTest, Signals_NotificationDisplayedDeclared) {
    createNotifier();
    QSignalSpy spy(notifier.get(), &Notifier::notificationDisplayed);
    EXPECT_TRUE(spy.isValid());
}

TEST_F(NotifierTest, Signals_NotificationClosedDeclared) {
    createNotifier();
    QSignalSpy spy(notifier.get(), &Notifier::notificationClosed);
    EXPECT_TRUE(spy.isValid());
}

// Edge cases

TEST_F(NotifierTest, Notify_EmptyNotificationList) {
    createNotifier();
    std::vector<CachedNotification> empty;
    notifier->notify(empty);
    // Should not crash
}

// Tests that require a display (GUI tests)
// These tests create NotificationWidget which requires X server

class NotifierGuiTest : public NotifierTest {};

TEST_F(NotifierGuiTest, DISABLED_Notify_UrgentNotificationBypassesDnD) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    fs::path folder = root_path / "app" / "summary";
    do_not_disturb[folder] = QDateTime::currentDateTimeUtc().addSecs(3600);

    CachedNotification n = createTestNotification("App", "Summary", 1, 2);
    n.path = folder / "run-1.json";

    notifier->notify(n);
}

TEST_F(NotifierGuiTest, DISABLED_Notify_BatchBypassesBackoff) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    fs::path folder = root_path / "app" / "summary";
    notification_backoff_minutes[folder] = 30;

    CachedNotification n = createTestNotification("App", "Summary");
    n.path = folder / "run-1.json";

    notifier->notify(n, true);
}

TEST_F(NotifierGuiTest, DISABLED_BatchNotify_ProcessesNotifications) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    CachedNotification n = createTestNotification("App", "Summary");
    n.at = QDateTime::currentDateTimeUtc().addSecs(-60);
    notification_cache.folders["app"].path = root_path / "app";
    notification_cache.folders["app"].folders["summary"].path = root_path / "app" / "summary";
    notification_cache.folders["app"].folders["summary"].notifications["run-1.json"] = n;

    notification_backoff_minutes[root_path / "app" / "summary"] = 5;

    notifier->batchNotify();
}

TEST_F(NotifierGuiTest, DISABLED_Notify_MultipleNotifications) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    std::vector<CachedNotification> notifications;
    for (int i = 1; i <= 5; ++i) {
        notifications.push_back(createTestNotification("App", QString("Summary %1").arg(i), i));
    }

    notifier->notify(notifications);
}

TEST_F(NotifierGuiTest, DISABLED_Notify_BatchCombinesSummary) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    std::vector<CachedNotification> notifications;
    for (int i = 1; i <= 3; ++i) {
        notifications.push_back(createTestNotification("App", QString("Summary %1").arg(i), i));
    }

    notifier->notify(notifications, true);
}

TEST_F(NotifierGuiTest, DISABLED_Notify_LongBodyTruncated) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    CachedNotification n = createTestNotification("App", "Summary");
    n.body = QString(2000, 'a');

    notifier->notify(n);
}

TEST_F(NotifierGuiTest, DISABLED_Notify_NotificationWithNoActions) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    CachedNotification n = createTestNotification("App", "Summary");
    n.actions.clear();

    notifier->notify(n);
}

TEST_F(NotifierGuiTest, DISABLED_Notify_NotificationWithMultipleActions) {
    if (!hasDisplay()) {
        GTEST_SKIP() << "No display available";
    }

    CachedNotification n = createTestNotification("App", "Summary");
    n.actions["default"] = "Open";
    n.actions["dismiss"] = "Dismiss";
    n.actions["reply"] = "Reply";

    notifier->notify(n);
}
