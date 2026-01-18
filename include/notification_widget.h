#pragma once

#include "notification_timer.h"
#include "notification_types.h"

#include <QIcon>
#include <QWidget>

// Forward declaration for UI class generated from .ui file
namespace Ui {
class NotificationWidget;
}

class NotificationWidget : public QWidget {
    Q_OBJECT

public:
    explicit NotificationWidget(const CachedNotification& data, QWidget* parent = nullptr);
    ~NotificationWidget();

    CachedNotification data;
    bool was_displayed = false;

signals:
    void actionInvoked(const QString& key);
    void displayed();
    void closed(int reason);
    void snoozed(int duration_ms);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUi();
    void setValues();
    void scheduleClose();
    void snoozeNotification(int duration_ms);
    QPixmap getPixmapFromHint(const QVariant& argument) const;
    QPixmap getPixmapFromString(const QString& str) const;

    Ui::NotificationWidget* ui;
    NotificationTimer* m_timer;

private slots:
    void setWasDisplayed();
    void closeButton_clicked();
};
