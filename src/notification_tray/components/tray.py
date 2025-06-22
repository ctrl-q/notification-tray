import json
import logging
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Iterable

from PyQt5.QtCore import Qt
from PyQt5.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PyQt5.QtWidgets import QAction, QApplication, QMenu, QSystemTrayIcon

from notification_tray.components.notification_cacher import NotificationCacher
from notification_tray.components.notifier import Notifier
from notification_tray.types.notification import NotificationFolder
from notification_tray.utils import settings
from notification_tray.utils.logging import log_input_and_output
from notification_tray.utils.settings import (get_notification_backoff_minutes,
                                              is_do_not_disturb_active,
                                              is_hide_from_tray_active)

from ..types.notification import CachedNotification

logger = logging.getLogger(__name__)


class Tray:
    def __init__(
        self,
        root_path: Path,
        do_not_disturb: settings.Cache,
        hide_from_tray: settings.Cache,
        notification_backoff_minutes: dict[Path, int],
        notifier: Notifier,
        notification_cacher: NotificationCacher,
        app: QApplication,
    ) -> None:
        self.do_not_disturb = do_not_disturb
        self.hide_from_tray = hide_from_tray
        self.notification_backoff_minutes = notification_backoff_minutes
        self.root_path = root_path
        self.tray_icon: QSystemTrayIcon | None = None
        self.tray_menu = QMenu()
        self.notifier = notifier
        self.app = app
        self.notification_cacher = notification_cacher
        logger.info(f"Started tray with root path {root_path}")

    def update_icon(self):
        logger.debug("Updating tray icon")

        def count_dir(dir_: NotificationFolder) -> int:
            logger.debug(f"Counting notifications in {dir_['path']}")
            if is_do_not_disturb_active(
                self.root_path, dir_["path"], self.do_not_disturb
            ) or is_hide_from_tray_active(
                self.root_path, dir_["path"], self.hide_from_tray
            ):
                logger.debug(f"DnD or hide from tray is active. Skipping")
                return 0
            else:
                notification_backoff_minutes = get_notification_backoff_minutes(
                    self.root_path, dir_["path"], self.notification_backoff_minutes
                )
                return sum(
                    1
                    for n in dir_["notifications"].values()
                    if not n.get("trashed")
                    and (
                        notification_backoff_minutes <= 0
                        or (datetime.now(UTC) - n["at"]).total_seconds() // 60
                        > notification_backoff_minutes
                    )
                ) + sum(
                    map(count_dir, dir_["folders"].values()),
                )

        file_count = count_dir(self.notification_cacher.notification_cache)
        if file_count == 0:
            if self.tray_icon is not None:
                self.tray_icon.hide()
        else:
            if self.tray_icon is None:
                self.tray_icon = QSystemTrayIcon(parent=self.app)
            self.tray_icon.setIcon(self.get_notification_badge(file_count))
            self.tray_icon.activated.connect(self.setup_tray_menu)
            self.tray_icon.show()
            self.tray_icon.setContextMenu(self.tray_menu)

    def get_notification_badge(self, number: int) -> QIcon:
        # Create a pixmap with a transparent background
        pixmap = QPixmap(40, 40)
        pixmap.fill(Qt.GlobalColor.transparent)

        # Create a painter
        painter = QPainter(pixmap)

        # Set up the font
        font = QFont()
        font.setPixelSize(24)
        font.setBold(True)
        painter.setFont(font)

        # Draw the red circle
        painter.setBrush(QColor(255, 0, 0))  # Red color
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawEllipse(0, 0, 40, 40)

        # Set the color for the text (white)
        painter.setPen(QColor(255, 255, 255))

        # Draw the number in the center of the circle
        painter.drawText(pixmap.rect(), Qt.AlignmentFlag.AlignCenter, str(number))

        # End painting
        painter.end()

        # Create and return the icon
        return QIcon(pixmap)

    def setup_tray_menu(self):
        self.tray_menu.clear()
        self.populate_submenu(self.tray_menu, self.notification_cacher.notification_cache)

        # Add exit option
        exit_action = QAction("Exit", self.tray_menu)
        exit_action.triggered.connect(self.app.quit)
        self.tray_menu.addAction(exit_action)  # type: ignore

    def add_directory_contents(self, path: Path, menu: QMenu):
        def has_notifications(dir_: NotificationFolder) -> bool:
            return (
                not is_do_not_disturb_active(
                    self.root_path, dir_["path"], self.do_not_disturb
                )
                and not is_hide_from_tray_active(
                    self.root_path, dir_["path"], self.hide_from_tray
                )
                and (
                    any(not n.get("trashed") for n in dir_["notifications"].values())
                    or (any(map(has_notifications, dir_["folders"].values())))
                )
            )

        notifications_cache = self.notification_cacher.notification_cache
        for folder in path.relative_to(self.root_path).parts:
            notifications_cache = notifications_cache["folders"][folder]
        for name, folder in sorted(notifications_cache["folders"].items()):
            if has_notifications(folder):
                submenu = QMenu(name, menu)
                menu.addMenu(submenu)
                placeholder = QAction("Loading...", submenu)
                submenu.addAction(placeholder)  # type: ignore
                submenu.aboutToShow.connect(
                    lambda sub=submenu, f=folder: self.populate_submenu(sub, f)  # type: ignore
                )
        for name, notification in notifications_cache["notifications"].items():
            if not notification.get("trashed"):
                file_action = QAction(name, menu)
                file_action.triggered.connect(
                    lambda checked, p=notification: self.notifier.notify(p, is_batch=True)  # type: ignore
                )
                menu.addAction(file_action)  # type: ignore

    def populate_submenu(self, submenu: QMenu, folder: NotificationFolder):
        path = folder["path"]
        if not (actions := submenu.actions()) or actions[0].text() == "Loading...":
            submenu.clear()
            self.add_directory_contents(path, submenu)

            # Re-add the "Move to Trash" action after populating
            trash_action = QAction("Move to Trash", submenu)
            trash_action.triggered.connect(
                lambda checked, p=path: self.notification_cacher.trash(p)  # type: ignore
            )
            submenu.addAction(trash_action)  # type: ignore

            show_all_action = QAction("Show All", submenu)
            show_all_action.triggered.connect(
                lambda checked, f=folder: self.notify(f)  # type: ignore
            )
            submenu.addAction(show_all_action)  # type: ignore

            # Add "Do Not Disturb" submenu
            dnd_submenu = QMenu("Do Not Disturb", submenu)
            submenu.addMenu(dnd_submenu)
            for text, duration in [
                ("1 hour", datetime.now(UTC) + timedelta(hours=1)),
                ("8 hours", datetime.now(UTC) + timedelta(hours=8)),
                ("Forever", datetime(9999, 1, 1, tzinfo=UTC)),
            ]:
                dnd_action = QAction(text, dnd_submenu)
                dnd_action.triggered.connect(
                    lambda checked, p=str(path), d=duration: self.update_datetime_setting(  # type: ignore
                        "do_not_disturb", p, d, cache=self.do_not_disturb
                    )  # type: ignore
                )
                dnd_submenu.addAction(dnd_action)  # type: ignore

            # Add "Hide From Tray" submenu
            dnd_submenu = QMenu("Hide From Tray", submenu)
            submenu.addMenu(dnd_submenu)
            for text, duration in [
                ("1 hour", datetime.now(UTC) + timedelta(hours=1)),
                ("8 hours", datetime.now(UTC) + timedelta(hours=8)),
                ("Forever", datetime(9999, 1, 1, tzinfo=UTC)),
            ]:
                dnd_action = QAction(text, dnd_submenu)
                dnd_action.triggered.connect(
                    lambda checked, p=str(path), d=duration: self.update_datetime_setting(  # type: ignore
                        "hide_from_tray_until", p, d, cache=self.hide_from_tray
                    )  # type: ignore
                )
                dnd_submenu.addAction(dnd_action)  # type: ignore

            # Add "Batch notifications" submenu
            dnd_submenu = QMenu("Batch Notifications", submenu)
            submenu.addMenu(dnd_submenu)
            for text, minutes in [
                ("Every minute", 1),
                ("Every 5 minutes", 5),
                ("Every 10 minutes", 10),
            ]:
                dnd_action = QAction(text, dnd_submenu)
                dnd_action.triggered.connect(
                    lambda checked, p=path, m=minutes: self.update_notification_backoff_minutes(  # type: ignore
                        p, m
                    )  # type: ignore
                )
                dnd_submenu.addAction(dnd_action)  # type: ignore

    def notify(self, folder: NotificationFolder):
        def get_notifications(
            folder: NotificationFolder,
        ) -> Iterable[CachedNotification]:
            yield from folder["notifications"].values()
            for folder in folder["folders"].values():
                yield from get_notifications(folder)

        logger.info(f"Displaying all notifications for folder {folder['path']}")
        self.notifier.notify(*get_notifications(folder), is_batch=True)

    @log_input_and_output(logging.DEBUG)
    def update_notification_backoff_minutes(self, folder_path: Path, minutes: int):
        folder_path_ = Path(folder_path).absolute()
        settings_file = folder_path_ / ".settings.json"
        self.notification_backoff_minutes[folder_path_] = minutes
        try:
            existing_settings = json.loads(settings_file.read_text())
        except FileNotFoundError:
            existing_settings = {}
        settings_file.write_text(
            json.dumps(existing_settings | {"notification_backoff_minutes": minutes})
        )

    @log_input_and_output(logging.DEBUG)
    def update_datetime_setting(
        self,
        setting_name: str,
        folder_path: str,
        until: datetime,
        cache: settings.Cache,
    ):
        settings.write_datetime_setting(folder_path, setting_name, until, cache=cache)
        settings.cache_datetime_setting(Path(folder_path), setting_name, cache=cache)
        self.update_icon()
        self.setup_tray_menu()

    def refresh(self) -> None:
        self.update_icon()
        self.setup_tray_menu()
