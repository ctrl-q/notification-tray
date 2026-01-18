#pragma once

#include <QDateTime>
#include <QTimer>

/**
 * A timer with pause/resume functionality.
 * Based on lxqt-notificationd's NotificationTimer.
 */
class NotificationTimer : public QTimer {
    Q_OBJECT

public:
    explicit NotificationTimer(QObject* parent = nullptr);

public slots:
    void start(int msec);
    void pause();
    void resume();

private:
    QDateTime m_startTime;
    qint64 m_intervalMsec;
};
