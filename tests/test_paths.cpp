#include "notification_types.h"
#include "utils/paths.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

class PathsTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(temp_dir->isValid());
        root_path = fs::path(temp_dir->path().toStdString());
    }

    void TearDown() override { temp_dir.reset(); }

    Notification createTestNotification(const QString& app_name, const QString& summary, int id = 1,
                                        const QString& run_id = "test-run-id") {
        Notification n;
        n.app_name = app_name;
        n.summary = summary;
        n.body = "Test body";
        n.app_icon = "test-icon";
        n.id = id;
        n.replaces_id = 0;
        n.expire_timeout = -1;
        n.notification_tray_run_id = run_id;
        n.at = QDateTime::currentDateTimeUtc();
        return n;
    }

    std::unique_ptr<QTemporaryDir> temp_dir;
    fs::path root_path;
};

// Tests for slugify

TEST_F(PathsTest, Slugify_SimpleText) {
    EXPECT_EQ(Paths::slugify("Hello World"), "hello-world");
}

TEST_F(PathsTest, Slugify_Uppercase) {
    EXPECT_EQ(Paths::slugify("UPPERCASE TEXT"), "uppercase-text");
}

TEST_F(PathsTest, Slugify_SpecialCharacters) {
    EXPECT_EQ(Paths::slugify("Hello! @#$% World?"), "hello-world");
}

TEST_F(PathsTest, Slugify_MultipleSpaces) {
    EXPECT_EQ(Paths::slugify("Hello    World"), "hello-world");
}

TEST_F(PathsTest, Slugify_MultipleHyphens) {
    EXPECT_EQ(Paths::slugify("Hello---World"), "hello-world");
}

TEST_F(PathsTest, Slugify_LeadingTrailingHyphens) {
    EXPECT_EQ(Paths::slugify("---Hello World---"), "hello-world");
}

TEST_F(PathsTest, Slugify_LeadingTrailingUnderscores) {
    EXPECT_EQ(Paths::slugify("___Hello World___"), "hello-world");
}

TEST_F(PathsTest, Slugify_Numbers) {
    EXPECT_EQ(Paths::slugify("Test 123 Numbers"), "test-123-numbers");
}

TEST_F(PathsTest, Slugify_Underscores) {
    EXPECT_EQ(Paths::slugify("hello_world_test"), "hello_world_test");
}

TEST_F(PathsTest, Slugify_MixedWhitespace) {
    EXPECT_EQ(Paths::slugify("Hello\tWorld\nTest"), "hello-world-test");
}

TEST_F(PathsTest, Slugify_AccentedCharacters) {
    // Unicode normalization should decompose accented characters
    EXPECT_EQ(Paths::slugify("Héllo Wörld"), "hello-world");
}

TEST_F(PathsTest, Slugify_EmptyString) {
    EXPECT_EQ(Paths::slugify(""), "unnamed");
}

TEST_F(PathsTest, Slugify_OnlySpecialChars) {
    EXPECT_EQ(Paths::slugify("!@#$%^&*()"), "unnamed");
}

TEST_F(PathsTest, Slugify_Whitespace) {
    EXPECT_EQ(Paths::slugify("   "), "unnamed");
}

TEST_F(PathsTest, Slugify_AppName) {
    EXPECT_EQ(Paths::slugify("Firefox Web Browser"), "firefox-web-browser");
}

TEST_F(PathsTest, Slugify_EmailSummary) {
    EXPECT_EQ(Paths::slugify("New Email: Meeting Tomorrow"), "new-email-meeting-tomorrow");
}

// Tests for getOutputPath

TEST_F(PathsTest, GetOutputPath_BasicPath) {
    Notification n = createTestNotification("Firefox", "New Tab");

    fs::path result = Paths::getOutputPath(root_path, n);

    // Should be: root/firefox/new-tab/{run_id}-{id}.json
    EXPECT_TRUE(result.string().find("firefox") != std::string::npos);
    EXPECT_TRUE(result.string().find("new-tab") != std::string::npos);
    EXPECT_TRUE(result.string().find(".json") != std::string::npos);
    EXPECT_TRUE(result.string().find("test-run-id") != std::string::npos);
    EXPECT_TRUE(result.string().find("-1.json") != std::string::npos);
}

TEST_F(PathsTest, GetOutputPath_SpecialCharsInNames) {
    Notification n = createTestNotification("Firefox! Browser", "New Email: Subject");

    fs::path result = Paths::getOutputPath(root_path, n);

    // Special chars should be removed in slugified path
    EXPECT_TRUE(result.string().find("firefox-browser") != std::string::npos);
    EXPECT_TRUE(result.string().find("new-email-subject") != std::string::npos);
}

TEST_F(PathsTest, GetOutputPath_DifferentIds) {
    Notification n1 = createTestNotification("App", "Summary", 1);
    Notification n2 = createTestNotification("App", "Summary", 42);

    fs::path result1 = Paths::getOutputPath(root_path, n1);
    fs::path result2 = Paths::getOutputPath(root_path, n2);

    EXPECT_TRUE(result1.string().find("-1.json") != std::string::npos);
    EXPECT_TRUE(result2.string().find("-42.json") != std::string::npos);
    EXPECT_NE(result1, result2);
}

TEST_F(PathsTest, GetOutputPath_DifferentRunIds) {
    Notification n1 = createTestNotification("App", "Summary", 1, "run-aaa");
    Notification n2 = createTestNotification("App", "Summary", 1, "run-bbb");

    fs::path result1 = Paths::getOutputPath(root_path, n1);
    fs::path result2 = Paths::getOutputPath(root_path, n2);

    EXPECT_TRUE(result1.string().find("run-aaa") != std::string::npos);
    EXPECT_TRUE(result2.string().find("run-bbb") != std::string::npos);
    EXPECT_NE(result1, result2);
}

TEST_F(PathsTest, GetOutputPath_PathUnderRoot) {
    Notification n = createTestNotification("App", "Summary");

    fs::path result = Paths::getOutputPath(root_path, n);

    // Result should be under root_path
    std::string root_str = root_path.string();
    std::string result_str = result.string();
    EXPECT_EQ(result_str.find(root_str), 0u);
}

TEST_F(PathsTest, GetOutputPath_HasJsonExtension) {
    Notification n = createTestNotification("App", "Summary");

    fs::path result = Paths::getOutputPath(root_path, n);

    EXPECT_EQ(result.extension(), ".json");
}

TEST_F(PathsTest, GetOutputPath_EmptyAppName) {
    Notification n = createTestNotification("", "Summary");

    fs::path result = Paths::getOutputPath(root_path, n);

    // Empty app name becomes "unnamed"
    EXPECT_TRUE(result.string().find("unnamed") != std::string::npos);
}

TEST_F(PathsTest, GetOutputPath_EmptySummary) {
    Notification n = createTestNotification("App", "");

    fs::path result = Paths::getOutputPath(root_path, n);

    // Empty summary becomes "unnamed"
    std::string result_str = result.string();
    size_t app_pos = result_str.find("app");
    size_t unnamed_pos = result_str.find("unnamed");
    EXPECT_NE(app_pos, std::string::npos);
    // unnamed should appear after app in the path
    EXPECT_TRUE(unnamed_pos > app_pos);
}

TEST_F(PathsTest, GetOutputPath_ConsistentOutput) {
    Notification n = createTestNotification("Firefox", "New Tab", 5, "run-xyz");

    fs::path result1 = Paths::getOutputPath(root_path, n);
    fs::path result2 = Paths::getOutputPath(root_path, n);

    // Same notification should produce same path
    EXPECT_EQ(result1, result2);
}
