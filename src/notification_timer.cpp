#include "notification_timer.h"

#include <algorithm>

NotificationTimer::NotificationTimer(QObject* parent) : QTimer(parent), m_intervalMsec(-1) {}

void NotificationTimer::start(int msec) {
    m_startTime = QDateTime::currentDateTime();
    m_intervalMsec = msec;
    QTimer::start(msec);
}

void NotificationTimer::pause() {
    if (!isActive())
        return;

    stop();
}

void NotificationTimer::resume() {
    if (isActive())
        return;

    m_intervalMsec -= m_startTime.msecsTo(QDateTime::currentDateTime());
    start(std::max(m_intervalMsec, static_cast<qint64>(1000)));
}
