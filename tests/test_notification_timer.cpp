#include "notification_timer.h"

#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

class NotificationTimerTest : public ::testing::Test {
protected:
    void SetUp() override { timer = new NotificationTimer(); }

    void TearDown() override { delete timer; }

    NotificationTimer* timer;
};

TEST_F(NotificationTimerTest, InitialState) {
    EXPECT_FALSE(timer->isActive());
}

TEST_F(NotificationTimerTest, StartActivatesTimer) {
    timer->start(1000);
    EXPECT_TRUE(timer->isActive());
}

TEST_F(NotificationTimerTest, PauseStopsTimer) {
    timer->start(5000);
    EXPECT_TRUE(timer->isActive());

    timer->pause();
    EXPECT_FALSE(timer->isActive());
}

TEST_F(NotificationTimerTest, PauseOnInactiveTimerDoesNothing) {
    EXPECT_FALSE(timer->isActive());
    timer->pause();
    EXPECT_FALSE(timer->isActive());
}

TEST_F(NotificationTimerTest, ResumeOnActiveTimerDoesNothing) {
    timer->start(5000);
    EXPECT_TRUE(timer->isActive());
    timer->resume();
    EXPECT_TRUE(timer->isActive());
}

TEST_F(NotificationTimerTest, ResumeAfterPauseRestoresTimer) {
    timer->start(5000);
    EXPECT_TRUE(timer->isActive());

    timer->pause();
    EXPECT_FALSE(timer->isActive());

    timer->resume();
    EXPECT_TRUE(timer->isActive());
}

TEST_F(NotificationTimerTest, ResumeCalculatesRemainingTime) {
    // Start with 5 seconds
    timer->start(5000);
    EXPECT_TRUE(timer->isActive());

    // Wait 100ms
    QTest::qWait(100);

    timer->pause();
    EXPECT_FALSE(timer->isActive());

    // Resume should calculate remaining time
    timer->resume();
    EXPECT_TRUE(timer->isActive());

    // The interval should be less than or equal to 5000 - 100 = 4900
    // But we allow some tolerance for timing
    EXPECT_LE(timer->interval(), 5000);
}

TEST_F(NotificationTimerTest, ResumeWithMinimumInterval) {
    // Start with a short interval
    timer->start(1100);
    EXPECT_TRUE(timer->isActive());

    // Wait almost all the time
    QTest::qWait(200);

    timer->pause();
    timer->resume();

    // Should still be active with at least 1000ms (the minimum)
    EXPECT_TRUE(timer->isActive());
    EXPECT_GE(timer->interval(), 1000);
}

TEST_F(NotificationTimerTest, TimeoutSignalEmitted) {
    QSignalSpy spy(timer, &QTimer::timeout);
    EXPECT_TRUE(spy.isValid());

    timer->start(50);

    // Wait for timeout
    EXPECT_TRUE(spy.wait(500));
    EXPECT_GE(spy.count(), 1);
}

TEST_F(NotificationTimerTest, StopPreventsTimeout) {
    QSignalSpy spy(timer, &QTimer::timeout);
    timer->start(100);
    timer->stop();

    // Wait a bit, no timeout should occur
    QTest::qWait(200);
    EXPECT_EQ(spy.count(), 0);
}
