import logging
from datetime import UTC, datetime
from functools import partial
from pathlib import Path

import libdbus_to_json
import libdbus_to_json.do_not_disturb
from PyQt5.QtCore import QObject, QUrl, pyqtSignal
from PyQt5.QtGui import QScreen
from PyQt5.QtMultimedia import QMediaContent, QMediaPlayer
from PyQt5.QtWidgets import QApplication

from notification_tray.components.notification_service import \
    NotificationCloseReason
from notification_tray.components.notification_widget import NotificationWidget
from notification_tray.types.notification import (CachedNotification,
                                                  NotificationFolder)
from notification_tray.utils.settings import (get_do_not_disturb,
                                              get_notification_backoff_minutes,
                                              is_do_not_disturb_active)

logger = logging.getLogger(__name__)


class Notifier(QObject):
    action_invoked = pyqtSignal(int, str)
    notification_closed = pyqtSignal(int, int, str, bool)
    notification_sounds: set[Path] = set()
    last_notified: dict[Path, int] = {}
    started_at = datetime.now(UTC)
    notification_widgets = set[NotificationWidget]()
    offset = 0

    def __init__(
        self,
        root_path: Path,
        do_not_disturb: libdbus_to_json.do_not_disturb.Cache,
        notification_backoff_minutes: dict[Path, int],
        notification_cache: NotificationFolder,
        parent: QObject | None = None,
    ) -> None:
        self.started_at = datetime.now(UTC)
        self.root_path = root_path
        self.do_not_disturb = do_not_disturb
        self.notification_backoff_minutes = notification_backoff_minutes
        self.notification_cache = notification_cache
        logger.info(f"Started notifier with root path {root_path}")
        super().__init__(parent)

    def show_or_queue_notification(self, notification_widget: NotificationWidget):
        logger.info(
            f"Got request to show notification {notification_widget.data['id']}"
        )
        self.notification_widgets.add(notification_widget)
        screen = QApplication.primaryScreen()
        match screen:
            case None:
                raise RuntimeError("No screen")
            case QScreen():
                screen = screen.availableGeometry()

        if (x := notification_widget.data.get("hints", {}).get("x")) is not None and (
            y := notification_widget.data.get("hints", {}).get("y")
        ) is not None:
            logger.debug(
                f"Notification {notification_widget.data['id']} has x,y hints ({x},{y}). Moving there"
            )
            notification_widget.move(x, y)

        else:
            if screen.height() - notification_widget.height() - self.offset > 0:
                logger.debug(
                    f"Moving notification {notification_widget.data['id']} to next available place"
                )
                notification_widget.move(
                    screen.width() - notification_widget.width(),
                    screen.height() - notification_widget.height() - self.offset,
                )
                self.offset += notification_widget.height() + 10
                notification_widget.show()
                notification_widget.displayed.emit()
            else:
                logger.debug(
                    f"No screen space left for notification {notification_widget.data['id']}. Queuing"
                )

    def close_notification(
        self,
        notification_widget: NotificationWidget,
        reason: int,
        is_batch: bool = False,
    ):
        logger.info(
            f"Closing notification {notification_widget.data['id']}. Reason {NotificationCloseReason(reason).name}"
        )
        if notification_widget.isVisible():
            notification_widget.close()
        self.notification_closed.emit(
            notification_widget.data["id"],
            reason,
            str(notification_widget.data["path"]),
            notification_widget.data["at"] >= self.started_at
            and not is_batch
            and notification_widget.data["id"] >= 0,
        )
        if notification_widget in self.notification_widgets:
            self.notification_widgets.remove(notification_widget)
        self.offset = sum(
            widget.height() + 10
            for widget in self.notification_widgets
            if widget.isVisible()
        )
        logger.info("Displaying queued notifications")
        for notification_widget in self.notification_widgets:
            self.show_or_queue_notification(notification_widget)

    def notify(
        self,
        notification: CachedNotification,
        *notifications: CachedNotification,
        is_batch: bool = False,
    ) -> None:
        logger.info(
            f"Got request to display {len([notification, *notifications])} notifications"
        )
        logger.info(
            "Checking which notifications should be displayed based on urgency, DnD and backoff settings"
        )
        all_notifications = [
            n
            for n in [notification, *notifications]
            if not n.get("trashed")
            and (
                n.get("hints", {}).get("urgency") == 2
                or (
                    not is_do_not_disturb_active(
                        self.root_path, n["path"].parent, self.do_not_disturb
                    )
                    and (
                        is_batch
                        or get_notification_backoff_minutes(
                            self.root_path,
                            n["path"].parent,
                            self.notification_backoff_minutes,
                        )
                        == 0
                    )
                )
            )
        ]
        logger.info(f"{len(all_notifications)} to display")
        if all_notifications:
            try:
                app_name = all_notifications[-1]["app_name"]
                content = (
                    all_notifications[-1]["body"]
                    if len(all_notifications) == 1
                    else "\n---\n".join(
                        map(
                            lambda d: "\n".join(
                                filter(
                                    None,
                                    [
                                        d["summary"],
                                        d["body"],
                                    ],
                                )
                            ),
                            all_notifications,
                        ),
                    )
                )
                # Truncate content if it's too long
                if len(content) >= 1000:
                    content = content[:997] + "..."

                self.play_notification_sound(all_notifications[-1])
                notification_widget = NotificationWidget(
                    CachedNotification(
                        notification,
                        summary=(
                            all_notifications[-1]["summary"]
                            if len(all_notifications) == 1
                            else f"{len(all_notifications)} new notifications from {app_name}"
                        ),
                        body=content,
                        expire_timeout=(
                            0
                            if any(
                                notification["expire_timeout"] == 0
                                for notification in all_notifications
                            )
                            else max(
                                -1,
                                sum(
                                    notification["expire_timeout"]
                                    for notification in all_notifications
                                ),
                            )
                        ),
                    )
                )
                notification_widget.closed.connect(
                    partial(
                        self.close_notification, notification_widget, is_batch=is_batch
                    )
                )
                notification_widget.action_invoked.connect(
                    lambda key: (
                        self.action_invoked.emit(notification_widget.data["id"], key)
                        if notification_widget.data["at"] >= self.started_at
                        else None
                    )
                )
                self.show_or_queue_notification(notification_widget)
                for notification in all_notifications:
                    parent = notification["path"].parent
                    logger.debug(f"Updating last_notified timestamp for {parent}")
                    self.last_notified[parent] = max(
                        self.last_notified.setdefault(parent, -1), notification["id"]
                    )

            except Exception as e:
                logger.exception(f"Unable to read notifications: {e!s}")
                self.notify(
                    CachedNotification(
                        summary="Error",
                        body=f"Unable to read notifications: {e!s}",
                        app_icon="error",
                        at=datetime.now(UTC),
                        hints={"sound-name": "dialog-error"},
                        actions=[],
                        app_name="notification-tray",
                        expire_timeout=-1,
                        id=-1,
                        replaces_id=0,
                        path=self.root_path / "error.json",
                    )
                )

    def batch_notify(self):
        def process(folder: NotificationFolder):
            logger.debug(f"Processing batch notifications for folder {folder['path']}")
            self.last_notified.setdefault(folder["path"], -1)
            new_notifications = list[CachedNotification]()
            logger.debug(f"{len(folder['notifications'])} notifications to process")
            for notification in folder["notifications"].values():
                at = notification["at"]
                id = notification["id"]
                logger.debug(f"Processing notification {id}")
                notification_backoff_minutes = get_notification_backoff_minutes(
                    self.root_path, folder["path"], self.notification_backoff_minutes
                )
                minutes_since_last_notification = (
                    datetime.now(UTC) - at
                ).total_seconds() // 60
                if not is_do_not_disturb_active(
                    self.root_path, folder["path"], self.do_not_disturb
                ) and (
                    (
                        notification_backoff_minutes > 0
                        and minutes_since_last_notification
                        <= notification_backoff_minutes
                    )
                    or (
                        # we just came back from DnD
                        (
                            do_not_disturb := get_do_not_disturb(
                                self.root_path, folder["path"], self.do_not_disturb
                            )
                        )
                        and do_not_disturb >= self.started_at
                        and at >= do_not_disturb
                        and id > self.last_notified[folder["path"]]
                    )
                ):
                    logger.debug("Adding notification to batch")
                    new_notifications.append(notification)
                else:
                    logger.debug("Skipping notification due to DnD or backoff settings")

            if new_notifications:
                # Hack: set expire_timeout to 0 so that self.notify uses that as the timeout for all notifications
                new_notifications[0] = CachedNotification(
                    new_notifications[0].copy(), expire_timeout=0
                )
                self.notify(*new_notifications, is_batch=True)
            for folder in folder["folders"].values():
                process(folder)

        process(self.notification_cache)

    def play_notification_sound(self, notification: CachedNotification):
        if notification.get("suppress-sound"):
            logger.debug(
                f"Notification {notification['id']} has suppress-sound hint. Not playing sound"
            )
        else:
            audio_path: str | None = None
            if sound_file := notification.get("hints", {}).get("sound-file"):
                audio_path = sound_file
                logger.debug(
                    f"Getting sound for {notification['id']} from sound-file hint"
                )
            elif sound_name := notification.get("hints", {}).get("sound-name"):
                audio_path = f"/usr/share/sounds/freedesktop/{sound_name}.oga"
                logger.debug(
                    f"Getting sound for {notification['id']} from sound-name hint"
                )
            else:
                for folder in [
                    notification["path"].parent,
                    *map(
                        self.root_path.joinpath,
                        notification["path"].parent.relative_to(self.root_path).parents,
                    ),
                ]:
                    if (
                        notification_sound := (folder / ".notification.wav")
                    ) in self.notification_sounds:
                        audio_path = str(notification_sound)
                        logger.debug(
                            f"Getting sound for {notification['id']} from .notification.wav hint"
                        )
                        break
            if audio_path:
                logger.debug(f"Notification sound: {audio_path}")
                player = QMediaPlayer()
                player.setMedia(QMediaContent(QUrl.fromLocalFile(audio_path)))
                player.play()
            else:
                logger.debug(
                    f"No notification sound found for notification {notification['id']}"
                )
