#include "notification_types.h"

#include <gtest/gtest.h>

class NotificationTypesTest : public ::testing::Test {};

// Tests for Notification struct

TEST_F(NotificationTypesTest, Notification_DefaultValues) {
    Notification n;
    EXPECT_TRUE(n.app_name.isEmpty());
    EXPECT_TRUE(n.summary.isEmpty());
    EXPECT_TRUE(n.body.isEmpty());
    EXPECT_TRUE(n.app_icon.isEmpty());
    EXPECT_TRUE(n.notification_tray_run_id.isEmpty());
    EXPECT_TRUE(n.actions.empty());
    EXPECT_TRUE(n.hints.empty());
}

TEST_F(NotificationTypesTest, Notification_SetValues) {
    Notification n;
    n.app_name = "Firefox";
    n.summary = "New Tab";
    n.body = "A new tab was opened";
    n.app_icon = "firefox";
    n.id = 42;
    n.replaces_id = 0;
    n.expire_timeout = 5000;
    n.notification_tray_run_id = "test-run-id";

    EXPECT_EQ(n.app_name, "Firefox");
    EXPECT_EQ(n.summary, "New Tab");
    EXPECT_EQ(n.body, "A new tab was opened");
    EXPECT_EQ(n.app_icon, "firefox");
    EXPECT_EQ(n.id, 42);
    EXPECT_EQ(n.replaces_id, 0);
    EXPECT_EQ(n.expire_timeout, 5000);
    EXPECT_EQ(n.notification_tray_run_id, "test-run-id");
}

TEST_F(NotificationTypesTest, Notification_Actions) {
    Notification n;
    n.actions["default"] = "Open";
    n.actions["dismiss"] = "Dismiss";
    n.actions["reply"] = "Reply";

    EXPECT_EQ(n.actions.size(), 3u);
    EXPECT_EQ(n.actions["default"], "Open");
    EXPECT_EQ(n.actions["dismiss"], "Dismiss");
    EXPECT_EQ(n.actions["reply"], "Reply");
}

TEST_F(NotificationTypesTest, Notification_Hints) {
    Notification n;
    n.hints["urgency"] = 2;
    n.hints["category"] = "email.arrived";
    n.hints["transient"] = true;

    EXPECT_EQ(n.hints.size(), 3);
    EXPECT_EQ(n.hints["urgency"].toInt(), 2);
    EXPECT_EQ(n.hints["category"].toString(), "email.arrived");
    EXPECT_EQ(n.hints["transient"].toBool(), true);
}

TEST_F(NotificationTypesTest, Notification_DateTime) {
    Notification n;
    QDateTime now = QDateTime::currentDateTimeUtc();
    n.at = now;

    EXPECT_EQ(n.at, now);
}

// Tests for CachedNotification struct

TEST_F(NotificationTypesTest, CachedNotification_Inherits) {
    CachedNotification cn;
    // Should have all Notification fields
    cn.app_name = "Test";
    cn.summary = "Summary";
    cn.id = 1;

    EXPECT_EQ(cn.app_name, "Test");
    EXPECT_EQ(cn.summary, "Summary");
    EXPECT_EQ(cn.id, 1);
}

TEST_F(NotificationTypesTest, CachedNotification_AdditionalFields) {
    CachedNotification cn;
    cn.path = "/home/user/notifications/app/summary.json";
    cn.closed_at = QDateTime::currentDateTimeUtc();
    cn.trashed = true;

    EXPECT_EQ(cn.path.string(), "/home/user/notifications/app/summary.json");
    EXPECT_TRUE(cn.closed_at.has_value());
    EXPECT_TRUE(cn.trashed);
}

TEST_F(NotificationTypesTest, CachedNotification_DefaultTrashed) {
    CachedNotification cn;
    EXPECT_FALSE(cn.trashed);
}

TEST_F(NotificationTypesTest, CachedNotification_OptionalClosedAt) {
    CachedNotification cn;
    EXPECT_FALSE(cn.closed_at.has_value());

    cn.closed_at = QDateTime::currentDateTimeUtc();
    EXPECT_TRUE(cn.closed_at.has_value());
}

// Tests for NotificationFolder struct

TEST_F(NotificationTypesTest, NotificationFolder_Empty) {
    NotificationFolder folder;
    EXPECT_TRUE(folder.folders.empty());
    EXPECT_TRUE(folder.notifications.empty());
}

TEST_F(NotificationTypesTest, NotificationFolder_AddSubfolder) {
    NotificationFolder root;
    root.path = "/notifications";

    NotificationFolder app_folder;
    app_folder.path = "/notifications/firefox";
    root.folders["firefox"] = app_folder;

    EXPECT_EQ(root.folders.size(), 1u);
    EXPECT_TRUE(root.folders.count("firefox") > 0);
    EXPECT_EQ(root.folders["firefox"].path.string(), "/notifications/firefox");
}

TEST_F(NotificationTypesTest, NotificationFolder_AddNotification) {
    NotificationFolder folder;
    folder.path = "/notifications/firefox";

    CachedNotification cn;
    cn.app_name = "Firefox";
    cn.summary = "New Tab";
    cn.id = 1;
    folder.notifications["notification1.json"] = cn;

    EXPECT_EQ(folder.notifications.size(), 1u);
    EXPECT_EQ(folder.notifications["notification1.json"].app_name, "Firefox");
}

TEST_F(NotificationTypesTest, NotificationFolder_NestedStructure) {
    NotificationFolder root;
    root.path = "/notifications";

    NotificationFolder app;
    app.path = "/notifications/firefox";

    NotificationFolder summary;
    summary.path = "/notifications/firefox/new-tab";

    CachedNotification n;
    n.app_name = "Firefox";
    n.id = 1;
    summary.notifications["1.json"] = n;

    app.folders["new-tab"] = summary;
    root.folders["firefox"] = app;

    EXPECT_EQ(root.folders["firefox"].folders["new-tab"].notifications["1.json"].app_name,
              "Firefox");
}

// Tests for NotificationCloseReason enum

TEST_F(NotificationTypesTest, NotificationCloseReason_Values) {
    EXPECT_EQ(static_cast<int>(NotificationCloseReason::EXPIRED), 1);
    EXPECT_EQ(static_cast<int>(NotificationCloseReason::DISMISSED_BY_USER), 2);
    EXPECT_EQ(static_cast<int>(NotificationCloseReason::CLOSED_BY_CALL_TO_CLOSENOTIFICATION), 3);
    EXPECT_EQ(static_cast<int>(NotificationCloseReason::UNDEFINED), 4);
}

TEST_F(NotificationTypesTest, NotificationCloseReason_Cast) {
    int reason = 2;
    NotificationCloseReason r = static_cast<NotificationCloseReason>(reason);
    EXPECT_EQ(r, NotificationCloseReason::DISMISSED_BY_USER);
}
