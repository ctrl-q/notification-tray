#include "notification_widget.h"

#include "ui_notification_widget.h"
#include "utils/logging.h"

#include <QDBusArgument>
#include <QFile>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <cstdlib>

static Logger logger = Logger::getLogger("NotificationWidget");

#define ICONSIZE QSize(32, 32)

static int getDefaultTimeout() {
    const char* env = std::getenv("NOTIFICATION_TRAY_DEFAULT_TIMEOUT_MILLIS");
    if (env) {
        int timeout = std::atoi(env);
        return (timeout > 0) ? timeout : 5000;
    }
    return 5000;
}

NotificationWidget::NotificationWidget(const CachedNotification& data, QWidget* parent)
    : QWidget(parent)
    , data(data)
    , ui(new Ui::NotificationWidget)
    , m_timer(nullptr) {
    setupUi();
    setValues();

    connect(this, &NotificationWidget::displayed, this, &NotificationWidget::setWasDisplayed);
}

NotificationWidget::~NotificationWidget() {
    delete ui;
}

void NotificationWidget::setupUi() {
    ui->setupUi(this);

    // Set object name to "Notification" so LXQt theme stylesheets apply correctly
    // (stylesheets use "Notification" selector, not "NotificationWidget")
    setObjectName("Notification");

    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::ToolTip);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);

    connect(ui->closeButton, &QToolButton::clicked, this, &NotificationWidget::closeButton_clicked);

    // Setup settings button with snooze menu
    QMenu* settingsMenu = new QMenu(this);
    QMenu* snoozeMenu = settingsMenu->addMenu("Snooze");

    QAction* snooze1min = snoozeMenu->addAction("1 minute");
    QAction* snooze5min = snoozeMenu->addAction("5 minutes");
    QAction* snooze30min = snoozeMenu->addAction("30 minutes");

    connect(snooze1min, &QAction::triggered, [this]() { snoozeNotification(60000); });
    connect(snooze5min, &QAction::triggered, [this]() { snoozeNotification(300000); });
    connect(snooze30min, &QAction::triggered, [this]() { snoozeNotification(1800000); });

    ui->settingsButton->setMenu(settingsMenu);
}

void NotificationWidget::setValues() {
    // Icon handling - following lxqt-notificationd's priority order:
    // 1. "image-data" hint
    // 2. "image_data" hint (compatibility)
    // 3. "image-path" hint
    // 4. "image_path" hint (compatibility)
    // 5. app_icon parameter
    // 6. "icon_data" hint (compatibility)

    QPixmap pixmap;

    if (!data.hints.value("image-data").isNull()) {
        pixmap = getPixmapFromHint(data.hints.value("image-data"));
    } else if (!data.hints.value("image_data").isNull()) {
        pixmap = getPixmapFromHint(data.hints.value("image_data"));
    } else if (!data.hints.value("image-path").isNull()) {
        pixmap = getPixmapFromString(data.hints.value("image-path").toString());
    } else if (!data.hints.value("image_path").isNull()) {
        pixmap = getPixmapFromString(data.hints.value("image_path").toString());
    } else if (!data.app_icon.isEmpty()) {
        pixmap = getPixmapFromString(data.app_icon);
    } else if (!data.hints.value("icon_data").isNull()) {
        pixmap = getPixmapFromHint(data.hints.value("icon_data"));
    }

    // Hide icon if not found (matches lxqt behavior - issue #325)
    if (pixmap.isNull()) {
        ui->iconLabel->hide();
    } else {
        if (pixmap.size().width() > ICONSIZE.width() ||
            pixmap.size().height() > ICONSIZE.height()) {
            pixmap = pixmap.scaled(ICONSIZE, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        ui->iconLabel->setPixmap(pixmap);
        ui->iconLabel->show();
    }

    // Application name
    ui->appLabel->setVisible(!data.app_name.isEmpty());
    ui->appLabel->setText(data.app_name);

    // Summary - hide if same as app name
    ui->summaryLabel->setVisible(!data.summary.isEmpty() && data.app_name != data.summary);
    ui->summaryLabel->setText(data.summary);

    // Body - convert newlines to <br/> for proper HTML rendering
    ui->bodyLabel->setVisible(!data.body.isEmpty());
    QString formattedBody = data.body;
    formattedBody.replace('\n', "<br/>");
    ui->bodyLabel->setText(formattedBody);

    // Timer setup
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }

    int urgency = data.hints.value("urgency", 1).toInt();
    // Don't auto-close critical notifications (urgency == 2) or those with timeout == 0
    if (data.expire_timeout != 0 && urgency != 2) {
        m_timer = new NotificationTimer(this);
        connect(m_timer, &NotificationTimer::timeout,
                [this]() { emit closed(static_cast<int>(NotificationCloseReason::EXPIRED)); });
        connect(this, &NotificationWidget::displayed, this, &NotificationWidget::scheduleClose);
    }

    // Actions - add buttons to actionsLayout
    // Match lxqt-notificationd's behavior: use NoFocus and set objectName
    for (const auto& [key, value] : data.actions) {
        QPushButton* button = new QPushButton(this);
        button->setObjectName(key);
        button->setFocusPolicy(Qt::NoFocus);
        if (data.hints.value("action-icons", false).toBool()) {
            button->setIcon(QIcon::fromTheme(value));
        } else {
            button->setText(value);
        }
        connect(button, &QPushButton::clicked, [this, key = key]() { emit actionInvoked(key); });
        ui->actionsLayout->addWidget(button);
    }

    adjustSize();
}

QPixmap NotificationWidget::getPixmapFromHint(const QVariant& argument) const {
    // Image data format from D-Bus spec:
    // (width, height, rowstride, has_alpha, bits_per_sample, channels, data)

    if (!argument.canConvert<QDBusArgument>()) {
        logger.warning("Image hint is not a QDBusArgument");
        return QPixmap();
    }

    int width, height, rowstride, bitsPerSample, channels;
    bool hasAlpha;
    QByteArray pixelData;

    const QDBusArgument arg = argument.value<QDBusArgument>();
    arg.beginStructure();
    arg >> width;
    arg >> height;
    arg >> rowstride;
    arg >> hasAlpha;
    arg >> bitsPerSample;
    arg >> channels;
    arg >> pixelData;
    arg.endStructure();

    bool rgb = !hasAlpha && channels == 3 && bitsPerSample == 8;
    QImage::Format imageFormat = rgb ? QImage::Format_RGB888 : QImage::Format_ARGB32;

    QImage img =
        QImage(reinterpret_cast<const uchar*>(pixelData.constData()), width, height, imageFormat);

    if (!rgb) {
        img = img.rgbSwapped();
    }

    return QPixmap::fromImage(img);
}

QPixmap NotificationWidget::getPixmapFromString(const QString& str) const {
    QUrl url(str);

    // Try as file URL first
    if (url.isValid() && QFile::exists(url.toLocalFile())) {
        logger.debug(QString("Loading icon from file URL: %1").arg(url.toLocalFile()));
        return QPixmap(url.toLocalFile());
    }

    // Try as file path (handles file:// prefix)
    if (str.startsWith("file://")) {
        QString path = str.mid(7);
        if (QFile::exists(path)) {
            logger.debug(QString("Loading icon from path: %1").arg(path));
            return QPixmap(path);
        }
    }

    // Try as absolute path
    if (QFile::exists(str)) {
        logger.debug(QString("Loading icon from path: %1").arg(str));
        return QPixmap(str);
    }

    // Fall back to theme icon
    logger.debug(QString("Loading icon from theme: %1").arg(str));
    QIcon themeIcon = QIcon::fromTheme(str);
    if (!themeIcon.isNull()) {
        return themeIcon.pixmap(ICONSIZE);
    }

    return QPixmap();
}

void NotificationWidget::mousePressEvent(QMouseEvent* event) {
    QString defaultAction;
    if (data.actions.size() == 1) {
        defaultAction = data.actions.begin()->first;
    } else if (data.actions.count("default")) {
        defaultAction = "default";
    }

    if (!defaultAction.isEmpty()) {
        emit actionInvoked(defaultAction);
    }

    QWidget::mousePressEvent(event);
}

void NotificationWidget::enterEvent(QEvent* event) {
    if (m_timer) {
        m_timer->pause();
    }
    QWidget::enterEvent(event);
}

void NotificationWidget::leaveEvent(QEvent* event) {
    if (m_timer) {
        m_timer->resume();
    }
    QWidget::leaveEvent(event);
}

void NotificationWidget::setWasDisplayed() {
    was_displayed = true;
}

void NotificationWidget::closeButton_clicked() {
    if (m_timer) {
        m_timer->stop();
    }
    emit closed(static_cast<int>(NotificationCloseReason::DISMISSED_BY_USER));
}

void NotificationWidget::snoozeNotification(int duration_ms) {
    logger.info(
        QString("Snoozing notification %1 for %2 seconds").arg(data.id).arg(duration_ms / 1000.0));
    if (m_timer) {
        m_timer->stop();
    }
    emit snoozed(duration_ms);
    hide();
}

void NotificationWidget::scheduleClose() {
    int timeout = (data.expire_timeout == -1) ? getDefaultTimeout() : data.expire_timeout;
    logger.info(QString("Scheduling close of notification %1 in %2 seconds")
                    .arg(data.id)
                    .arg(timeout / 1000.0));
    if (m_timer) {
        m_timer->start(timeout);
    }
}

void NotificationWidget::paintEvent(QPaintEvent*) {
    // Required for stylesheet backgrounds to work correctly on QWidget subclasses
    // (from lxqt-notificationd's Notification::paintEvent)
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
