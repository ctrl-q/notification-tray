#include "notification_service.h"

#include <QSignalSpy>
#include <QTemporaryDir>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

class NotificationServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(temp_dir->isValid());
        root_path = fs::path(temp_dir->path().toStdString());

        // Note: NotificationService registers with D-Bus, which may fail in test env
        // We'll test what we can without actual D-Bus
    }

    void TearDown() override {
        service.reset();
        temp_dir.reset();
    }

    std::unique_ptr<QTemporaryDir> temp_dir;
    fs::path root_path;
    QString run_id = "test-run-id";
    std::unique_ptr<NotificationService> service;
};

// Tests for Notify() - basic functionality

TEST_F(NotificationServiceTest, Notify_CreatesNotification) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("Firefox",      // app_name
                              0,              // replaces_id
                              "firefox",      // app_icon
                              "New Tab",      // summary
                              "Tab opened",   // body
                              QStringList{},  // actions
                              QVariantMap{},  // hints
                              -1              // expire_timeout
    );

    EXPECT_GT(id, 0u);
    EXPECT_TRUE(service->notifications.count(id) > 0);
}

TEST_F(NotificationServiceTest, Notify_StoresCorrectData) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("Firefox", 0, "firefox-icon", "New Tab", "Tab content", QStringList{},
                              QVariantMap{}, 5000);

    auto& n = service->notifications[id];
    EXPECT_EQ(n.app_name, "Firefox");
    EXPECT_EQ(n.app_icon, "firefox-icon");
    EXPECT_EQ(n.summary, "New Tab");
    EXPECT_EQ(n.body, "Tab content");
    EXPECT_EQ(n.expire_timeout, 5000);
}

TEST_F(NotificationServiceTest, Notify_IncrementingIds) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id1 = service->Notify("App", 0, "", "Summary 1", "", QStringList{}, QVariantMap{}, -1);
    uint id2 = service->Notify("App", 0, "", "Summary 2", "", QStringList{}, QVariantMap{}, -1);
    uint id3 = service->Notify("App", 0, "", "Summary 3", "", QStringList{}, QVariantMap{}, -1);

    EXPECT_EQ(id1, 1u);
    EXPECT_EQ(id2, 2u);
    EXPECT_EQ(id3, 3u);
}

TEST_F(NotificationServiceTest, Notify_ReplacesId) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id1 = service->Notify("App", 0, "", "Original", "", QStringList{}, QVariantMap{}, -1);
    uint id2 = service->Notify("App", id1, "", "Replaced", "", QStringList{}, QVariantMap{}, -1);

    // Should use the replaces_id
    EXPECT_EQ(id2, id1);
    EXPECT_EQ(service->notifications[id1].summary, "Replaced");
}

TEST_F(NotificationServiceTest, Notify_ParsesActions) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    QStringList actions = {"default", "Open", "dismiss", "Dismiss", "reply", "Reply"};
    uint id = service->Notify("App", 0, "", "Summary", "", actions, QVariantMap{}, -1);

    auto& n = service->notifications[id];
    EXPECT_EQ(n.actions.size(), 3u);
    EXPECT_EQ(n.actions["default"], "Open");
    EXPECT_EQ(n.actions["dismiss"], "Dismiss");
    EXPECT_EQ(n.actions["reply"], "Reply");
}

TEST_F(NotificationServiceTest, Notify_StoresHints) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    QVariantMap hints;
    hints["urgency"] = 2;
    hints["category"] = "email.arrived";
    hints["transient"] = true;

    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, hints, -1);

    auto& n = service->notifications[id];
    EXPECT_EQ(n.hints["urgency"].toInt(), 2);
    EXPECT_EQ(n.hints["category"].toString(), "email.arrived");
    EXPECT_TRUE(n.hints["transient"].toBool());
}

TEST_F(NotificationServiceTest, Notify_SetsRunId) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, QVariantMap{}, -1);

    EXPECT_EQ(service->notifications[id].notification_tray_run_id, run_id);
}

TEST_F(NotificationServiceTest, Notify_SetsTimestamp) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    QDateTime before = QDateTime::currentDateTimeUtc();
    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, QVariantMap{}, -1);
    QDateTime after = QDateTime::currentDateTimeUtc();

    auto& n = service->notifications[id];
    EXPECT_LE(before.secsTo(n.at), 1);
    EXPECT_GE(after.secsTo(n.at), -1);
}

TEST_F(NotificationServiceTest, Notify_SetsPath) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("Firefox", 0, "", "New Tab", "", QStringList{}, QVariantMap{}, -1);

    auto& n = service->notifications[id];
    EXPECT_FALSE(n.path.empty());
    EXPECT_TRUE(n.path.string().find(root_path.string()) == 0);
}

TEST_F(NotificationServiceTest, Notify_EmitsSignal) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    QSignalSpy spy(service->signaler, &NotificationServiceSignaler::notificationReady);
    ASSERT_TRUE(spy.isValid());

    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, QVariantMap{}, -1);

    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toInt(), static_cast<int>(id));
}

// Tests for CloseNotification()

TEST_F(NotificationServiceTest, CloseNotification_SetsClosedAt) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, QVariantMap{}, -1);
    EXPECT_FALSE(service->notifications[id].closed_at.has_value());

    service->CloseNotification(id);

    EXPECT_TRUE(service->notifications[id].closed_at.has_value());
}

TEST_F(NotificationServiceTest, CloseNotification_EmitsSignals) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, QVariantMap{}, -1);

    QSignalSpy closedSpy(service.get(), &NotificationService::NotificationClosed);
    QSignalSpy signalerSpy(service->signaler, &NotificationServiceSignaler::notificationClosed);

    service->CloseNotification(id);

    EXPECT_EQ(closedSpy.count(), 1);
    EXPECT_EQ(signalerSpy.count(), 1);
}

TEST_F(NotificationServiceTest, CloseNotification_AlreadyTrashed) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id = service->Notify("App", 0, "", "Summary", "", QStringList{}, QVariantMap{}, -1);
    service->notifications[id].trashed = true;

    QSignalSpy closedSpy(service.get(), &NotificationService::NotificationClosed);

    service->CloseNotification(id);

    // Should not emit signal for trashed notification
    EXPECT_EQ(closedSpy.count(), 0);
}

// Tests for CloseActiveNotifications()

TEST_F(NotificationServiceTest, CloseActiveNotifications_ClosesAll) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    service->Notify("App", 0, "", "Summary 1", "", QStringList{}, QVariantMap{}, -1);
    service->Notify("App", 0, "", "Summary 2", "", QStringList{}, QVariantMap{}, -1);
    service->Notify("App", 0, "", "Summary 3", "", QStringList{}, QVariantMap{}, -1);

    service->CloseActiveNotifications();

    for (auto& [id, n] : service->notifications) {
        EXPECT_TRUE(n.closed_at.has_value());
    }
}

TEST_F(NotificationServiceTest, CloseActiveNotifications_SkipsAlreadyClosed) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    uint id1 = service->Notify("App", 0, "", "Summary 1", "", QStringList{}, QVariantMap{}, -1);
    service->Notify("App", 0, "", "Summary 2", "", QStringList{}, QVariantMap{}, -1);

    // Close first one
    service->CloseNotification(id1);
    QDateTime first_close = service->notifications[id1].closed_at.value();

    // Close all
    service->CloseActiveNotifications();

    // First one should still have original close time
    EXPECT_EQ(service->notifications[id1].closed_at.value(), first_close);
}

// Tests for GetCapabilities()

TEST_F(NotificationServiceTest, GetCapabilities_ReturnsExpectedList) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    QStringList caps = service->GetCapabilities();

    EXPECT_TRUE(caps.contains("actions"));
    EXPECT_TRUE(caps.contains("body"));
    EXPECT_TRUE(caps.contains("body-hyperlinks"));
    EXPECT_TRUE(caps.contains("body-markup"));
    EXPECT_TRUE(caps.contains("persistence"));
    EXPECT_TRUE(caps.contains("sound"));
}

// Tests for GetServerInformation()

TEST_F(NotificationServiceTest, GetServerInformation_ReturnsInfo) {
    service = std::make_unique<NotificationService>(root_path, run_id);

    QString name, vendor, version, spec_version;
    service->GetServerInformation(name, vendor, version, spec_version);

    EXPECT_EQ(name, "notification-tray");
    EXPECT_EQ(vendor, "github.com");
    EXPECT_FALSE(version.isEmpty());
    EXPECT_EQ(spec_version, "1.3");
}
