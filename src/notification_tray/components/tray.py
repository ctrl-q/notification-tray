from datetime import UTC, datetime, timedelta
from pathlib import Path

import libdbus_to_json.do_not_disturb
from notification_tray.components.notification_cacher import NotificationCacher
from notification_tray.components.notifier import Notifier, Notify
from notification_tray.types.notification import NotificationFolder
from notification_tray.utils.settings import (is_do_not_disturb_active,
                                              is_hide_from_tray_active)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PyQt5.QtWidgets import QAction, QApplication, QMenu, QSystemTrayIcon


class Tray:
    def __init__(
        self,
        root_path: Path,
        do_not_disturb: libdbus_to_json.do_not_disturb.Cache,
        hide_from_tray: libdbus_to_json.do_not_disturb.Cache,
        notifier: Notifier,
        notification_cacher: NotificationCacher,
        app: QApplication,
    ) -> None:
        self.do_not_disturb = do_not_disturb
        self.hide_from_tray = hide_from_tray
        self.root_path = root_path
        self.tray_icon: QSystemTrayIcon | None = None
        self.tray_menu = QMenu()
        self.notifier = notifier
        self.app = app
        self.notification_cacher = notification_cacher
        self.notification_cacher.notification_cached.connect(self.refresh)
        self.refresh()

    def update_icon(self):
        def count_dir(dir_: NotificationFolder) -> int:
            if is_do_not_disturb_active(
                self.root_path, dir_["path"], self.do_not_disturb
            ) or is_hide_from_tray_active(
                self.root_path, dir_["path"], self.hide_from_tray
            ):
                return 0
            else:
                return len(dir_["notifications"]) + sum(
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
        self.add_directory_contents(self.root_path, self.tray_menu)

        # Add exit option
        exit_action = QAction("Exit", self.tray_menu)
        exit_action.triggered.connect(self.app.quit)
        self.tray_menu.addAction(exit_action) # type: ignore

    def add_directory_contents(self, path: Path, menu: QMenu):
        def has_notifications(dir_: NotificationFolder) -> bool:
            return (
                (
                    not is_do_not_disturb_active(
                        self.root_path, dir_["path"], self.do_not_disturb
                    )
                )
                and not is_hide_from_tray_active(
                    self.root_path, dir_["path"], self.hide_from_tray
                )
                and (
                    bool(dir_["notifications"])
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
                submenu.addAction(placeholder) # type: ignore
                submenu.aboutToShow.connect(
                    lambda sub=submenu, p=folder["path"]: self.populate_submenu(sub, p) # type: ignore
                )
        for name, notification in notifications_cache["notifications"].items():
            file_action = QAction(name, menu)
            file_action.triggered.connect(
                lambda checked, p=notification: self.notifier.notify([p]) # type: ignore
            )
            menu.addAction(file_action) # type: ignore

    def populate_submenu(self, submenu: QMenu, path: Path):
        if submenu.actions()[0].text() == "Loading...":
            submenu.clear()
            self.add_directory_contents(path, submenu)

            # Re-add the "Move to Trash" action after populating
            trash_action = QAction("Move to Trash", submenu)
            trash_action.triggered.connect(
                lambda checked, p=path: self.notification_cacher.trash(p) # type: ignore
            )
            submenu.addAction(trash_action) # type: ignore

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
                    lambda checked, p=str(path), d=duration: self.set_do_not_disturb( # type: ignore
                        p, d
                    ) # type: ignore
                )
                dnd_submenu.addAction(dnd_action) # type: ignore

    def set_do_not_disturb(self, folder_path: str, until: datetime):
        libdbus_to_json.do_not_disturb.write_datetime_setting(
            folder_path, "do_not_disturb_until", until, cache=self.do_not_disturb
        )
        self.notifier
        Notify(
            summary="Do Not Disturb",
            body=f"Do Not Disturb set until {until}",
            icon="dialog-information",
        ).set_server(self.notifier.server).show() # type: ignore

        self.refresh()

    def refresh(self) -> None:
        self.update_icon()
        self.setup_tray_menu()
