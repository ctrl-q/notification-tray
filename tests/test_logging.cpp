#include "utils/logging.h"

#include <gtest/gtest.h>
#include <sstream>

class LoggingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset log level to default
        Logger::setLogLevel(Logger::LOG_LEVEL_INFO);
    }
};

// Tests for log level filtering

TEST_F(LoggingTest, LogLevelDefault) {
    Logger logger = Logger::getLogger("TestLogger");
    // Default level is INFO, so debug should be filtered out
    // (can't easily test output, but we can test it doesn't crash)
    logger.debug("Debug message");
    logger.info("Info message");
    logger.warning("Warning message");
    logger.error("Error message");
}

TEST_F(LoggingTest, SetLogLevel_Debug) {
    Logger::setLogLevel(Logger::LOG_LEVEL_DEBUG);
    Logger logger = Logger::getLogger("TestLogger");
    // All levels should be logged
    logger.debug("Debug message");
    logger.info("Info message");
}

TEST_F(LoggingTest, SetLogLevel_Error) {
    Logger::setLogLevel(Logger::LOG_LEVEL_ERROR);
    Logger logger = Logger::getLogger("TestLogger");
    // Only error should be logged
    logger.debug("Debug message");
    logger.info("Info message");
    logger.warning("Warning message");
    logger.error("Error message");
}

TEST_F(LoggingTest, GetLogger_CreatesLogger) {
    Logger logger = Logger::getLogger("MyComponent");
    // Just verify it doesn't crash
    logger.info("Test message");
}

TEST_F(LoggingTest, GetLogger_DifferentNames) {
    Logger logger1 = Logger::getLogger("Component1");
    Logger logger2 = Logger::getLogger("Component2");
    // Both should work independently
    logger1.info("Message from 1");
    logger2.info("Message from 2");
}

TEST_F(LoggingTest, LogLevel_Enum) {
    // Verify enum values exist and have expected ordering
    EXPECT_LT(Logger::LOG_LEVEL_DEBUG, Logger::LOG_LEVEL_INFO);
    EXPECT_LT(Logger::LOG_LEVEL_INFO, Logger::LOG_LEVEL_WARNING);
    EXPECT_LT(Logger::LOG_LEVEL_WARNING, Logger::LOG_LEVEL_ERROR);
}

TEST_F(LoggingTest, LogWithEmptyMessage) {
    Logger logger = Logger::getLogger("TestLogger");
    // Should not crash with empty message
    logger.info("");
    logger.error("");
}

TEST_F(LoggingTest, LogWithSpecialCharacters) {
    Logger logger = Logger::getLogger("TestLogger");
    // Should handle special characters
    logger.info("Message with special chars: !@#$%^&*()");
    logger.info("Message with newline\nand tab\t");
    logger.info("Message with quotes \"and\" 'apostrophes'");
}

TEST_F(LoggingTest, LogWithUnicode) {
    Logger logger = Logger::getLogger("TestLogger");
    // Should handle unicode
    logger.info("Unicode: 日本語 中文 한국어");
    logger.info("Emoji: 🎉🚀");
}

TEST_F(LoggingTest, LoggerCopyable) {
    Logger logger1 = Logger::getLogger("TestLogger");
    Logger logger2 = logger1;  // Copy
    logger2.info("Message from copy");
}

TEST_F(LoggingTest, MultipleLogCalls) {
    Logger logger = Logger::getLogger("TestLogger");
    for (int i = 0; i < 100; ++i) {
        logger.info(QString("Message %1").arg(i));
    }
}

TEST_F(LoggingTest, LogMethod_DirectCall) {
    Logger logger = Logger::getLogger("TestLogger");
    logger.log(Logger::LOG_LEVEL_INFO, "Direct log call");
    logger.log(Logger::LOG_LEVEL_ERROR, "Error via direct call");
}

TEST_F(LoggingTest, LogWithLongMessage) {
    Logger logger = Logger::getLogger("TestLogger");
    QString long_message(10000, 'a');
    // Should not crash with very long message
    logger.info(long_message);
}

// Environment variable tests (can't easily set env vars in tests,
// but we can verify the init function exists and doesn't crash)

TEST_F(LoggingTest, InitFromEnvironment) {
    Logger::initFromEnvironment();
    Logger logger = Logger::getLogger("TestLogger");
    logger.info("After re-init");
}
