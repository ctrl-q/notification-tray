# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "desktop-notify",
#     "libdbus-to-json",
#     "pydub",
#     "pygobject==3.48.2",
#     "pyqt5==5.15.11",
#     "send2trash==1.8.3",
# ]
#
# [tool.uv.sources]
# libdbus-to-json = { path = "../libdbus-to-json", editable = true }
# ///
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime, timedelta
from pathlib import Path
from threading import Thread
from typing import Any, TypedDict

import libdbus_to_json
import libdbus_to_json.do_not_disturb
from desktop_notify import BaseNotify, glib
from pydub import AudioSegment
from pydub.playback import play
from PyQt5.QtCore import QFileSystemWatcher, QObject, Qt, QTimer, pyqtSlot
from PyQt5.QtDBus import QDBusConnection
from PyQt5.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PyQt5.QtWidgets import QAction, QApplication, QMenu, QSystemTrayIcon
from send2trash import send2trash


class Notification(TypedDict):
    app_name: str
    replaces_id: int
    app_icon: str
    summary: str
    body: str
    actions: list[str]
    hints: dict[str, Any]
    expire_timeout: int
    id: int
    path: str
    at: datetime


class NotificationFolder(TypedDict):
    folders: dict[str, "NotificationFolder"]
    notifications: dict[str, list[Notification]]
    path: Path


# the Notify was desktop_notify.glib incorrectly subclasses BaseServer, so I have to re-define it myself
class Notify(BaseNotify):

    def show(self):
        self.__id = self.server.show(self)

    def show_sync(self):
        self.__id = self.server.show_sync(self)

    def close(self):
        if self.__id:
            self.server.close(self)

    def close_sync(self):
        if self.__id:
            self.server.close_sync(self)

    def set_server(self, server: glib.Server):
        return super().set_server(server)

    @property
    def server_class(self):
        return glib.Server


class SystemTrayFileBrowser(QObject):
    def __init__(self, root_path: Path, parent: QObject | None = None):
        super().__init__(parent)
        self.app = QApplication([])
        self.root_path = root_path
        self.tray_icon = None
        self.tray_menu = QMenu()
        self.do_not_disturb: dict[Path, datetime | None] = {}
        self.last_notified: dict[Path, int] = {}
        self.notification_backoff_minutes: dict[Path, int] = {}
        self.notification_sounds: set[Path] = set()
        self.notifier = glib.Server("notification-tray")
        self.started_at = datetime.now(UTC)
        self.notification_cache = NotificationFolder(
            folders={}, notifications={}, path=self.root_path
        )
        self.cache_existing_notifications()
        self.setup_file_watcher()
        self.start_timer()
        self.refresh()
        QDBusConnection.sessionBus().connect(
            "",
            "/com/example/DbusNotificationsToJson/notifications",
            "com.example.DbusNotificationsToJson.NotificationSent",
            "NotificationSent",
            self.cache,
        )
        self.threads: list[Thread] = []
        self.app.aboutToQuit.connect(lambda: [thread.join() for thread in self.threads])

    @pyqtSlot(str)
    def cache(self, signal: str):
        notification = Notification(json.loads(signal), at=datetime.now(UTC))
        notifications = self.notification_cache
        for folder in (
            Path(notification["path"])
            .relative_to(self.notification_cache["path"])
            .parent.parts
        ):
            notifications = notifications["folders"].setdefault(
                folder,
                NotificationFolder(
                    folders={}, notifications={}, path=notifications["path"] / folder
                ),
            )
        path = Path(notification["path"])
        notifications["notifications"].setdefault(path.name, []).append(notification)
        if self.is_do_not_disturb_active(
            path.parent
        ) or self.get_notification_backoff_minutes(path.parent):
            # TODO couldn't figure out how to use QtDbus for this
            subprocess.call(
                [
                    "gdbus",
                    "call",
                    "--session",
                    "--dest=org.freedesktop.Notifications",
                    "--object-path=/org/freedesktop/Notifications",
                    "--method=org.freedesktop.Notifications.CloseNotification",
                    str(notification["id"]),
                ]
            )
        else:
            self.notify([notification])
        self.refresh()

    def refresh(self):
        self.update_icon()
        self.setup_tray_menu()

    def setup_file_watcher(self):
        self.watcher = QFileSystemWatcher(
            [
                str(self.root_path),
                *map(
                    str,
                    self.root_path.rglob(".settings.json"),
                ),
            ]
        )
        self.watcher.fileChanged.connect(self.on_settings_file_changed)
        for settings_file in self.watcher.files():
            self.on_settings_file_changed(settings_file)

    def start_timer(self):
        self.timer = QTimer()
        self.timer.setInterval(60000)  # Check every minute
        self.timer.timeout.connect(self.refresh)
        self.timer.timeout.connect(self.batch_notify)
        self.timer.start()

    def batch_notify(self):
        def process(folder: NotificationFolder):
            self.last_notified.setdefault(folder["path"], -1)
            new_notifications: list[Notification] = []
            for notifications in folder["notifications"].values():
                if notifications:
                    at = notifications[-1]["at"]
                    id = notifications[-1]["id"]
                    notification_backoff_minutes = (
                        self.get_notification_backoff_minutes(folder["path"])
                    )
                    minutes_since_last_notification = (
                        datetime.now(UTC) - at
                    ).total_seconds() // 60
                    if not self.is_do_not_disturb_active(folder["path"]) and (
                        (
                            notification_backoff_minutes > 0
                            and minutes_since_last_notification
                            <= notification_backoff_minutes
                        )
                        or (
                            # we just came back from DnD
                            (do_not_disturb := self.get_do_not_disturb(folder["path"]))
                            and do_not_disturb >= self.started_at
                            and at >= do_not_disturb
                            and id > self.last_notified[folder["path"]]
                        )
                    ):
                        new_notifications.extend(notifications)

            if new_notifications:
                self.notify(
                    new_notifications,
                    expires_in_milliseconds=9999999,
                )
            with ThreadPoolExecutor() as pool:
                pool.map(process, folder["folders"].values())

        process(self.notification_cache)

    def get_notification_backoff_minutes(self, folder_path: Path) -> int:
        for folder in [
            folder_path,
            *map(
                self.root_path.joinpath,
                folder_path.relative_to(self.root_path).parents,
            ),
        ]:
            if folder in self.notification_backoff_minutes:
                return self.notification_backoff_minutes[folder]
        return 0

    def on_settings_file_changed(self, path: str):
        path_ = Path(path)
        if path_.parent in self.do_not_disturb:
            del self.do_not_disturb[path_.parent]
        try:
            libdbus_to_json.do_not_disturb.cache_do_not_disturb(
                path_.parent, cache=self.do_not_disturb
            )
            if (
                notification_backoff_minutes := json.loads(path_.read_text()).get(
                    "notification_backoff_minutes"
                )
            ) is not None:
                self.notification_backoff_minutes[path_.parent] = int(
                    notification_backoff_minutes
                )
        except FileNotFoundError:
            pass

    def play_notification_sound(self, folder_path: Path):
        for folder in [
            folder_path,
            *map(
                self.root_path.joinpath, folder_path.relative_to(self.root_path).parents
            ),
        ]:
            if (
                notification_sound := (folder / ".notification.wav")
            ) in self.notification_sounds:
                play(AudioSegment.from_file(notification_sound))

    def setup_tray_menu(self):
        self.tray_menu.clear()
        self.add_directory_contents(self.root_path, self.tray_menu)

        # Add exit option
        exit_action = QAction("Exit", self.tray_menu)
        exit_action.triggered.connect(self.app.quit)
        self.tray_menu.addAction(exit_action)

    def add_directory_contents(self, path: Path, menu: QMenu):
        def has_notifications(dir_: NotificationFolder) -> bool:
            return (not self.is_do_not_disturb_active(dir_["path"])) and (
                (any(dir_["notifications"].values()))
                or (any(map(has_notifications, dir_["folders"].values())))
            )

        notifications_cache = self.notification_cache
        for folder in path.relative_to(self.root_path).parts:
            notifications_cache = notifications_cache["folders"][folder]
        for name, folder in sorted(notifications_cache["folders"].items()):
            if has_notifications(folder):
                submenu = QMenu(name, menu)
                menu.addMenu(submenu)
                placeholder = QAction("Loading...", submenu)
                submenu.addAction(placeholder)
                submenu.aboutToShow.connect(
                    lambda sub=submenu, p=folder["path"]: self.populate_submenu(sub, p)
                )
        for name, notifications in notifications_cache["notifications"].items():
            file_action = QAction(name, menu)
            file_action.triggered.connect(
                lambda checked, p=notifications: self.notify(p)
            )
            menu.addAction(file_action)

    def populate_submenu(self, submenu, path: Path):
        if submenu.actions()[0].text() == "Loading...":
            submenu.clear()
            self.add_directory_contents(path, submenu)

            # Re-add the "Move to Trash" action after populating
            trash_action = QAction("Move to Trash", submenu)
            trash_action.triggered.connect(lambda checked, p=path: self.trash(p))
            submenu.addAction(trash_action)

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
                    lambda checked, p=path, d=duration: self.set_do_not_disturb(p, d)
                )
                dnd_submenu.addAction(dnd_action)

    def notify(
        self, notifications: list[Notification], expires_in_milliseconds: int = 5000
    ):
        try:
            if notifications:
                app_name = notifications[-1]["app_name"]
                content = "\n---\n".join(
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
                        notifications,
                    ),
                )
                # Truncate content if it's too long
                if len(content) >= 1000:
                    content = content[:997] + "..."

                self.play_notification_sound(Path(notifications[-1]["path"]).parent)
                Notify(
                    summary=(
                        app_name
                        if len(notifications) == 1
                        else f"{len(notifications)} new notifications from {app_name}"
                    ),
                    body=content,
                    timeout=max(
                        *(int(d.get("expire_timeout", -1)) for d in notifications),
                        expires_in_milliseconds,
                    ),
                ).set_id(notifications[-1]["id"]).set_server(self.notifier).show()
                for notification in notifications:
                    parent = Path(notification["path"]).parent
                    self.last_notified[parent] = max(
                        self.last_notified.setdefault(parent, -1), notification["id"]
                    )

        except Exception as e:
            self.play_notification_sound(Path(notifications[-1]["path"]).parent)
            Notify(
                summary="Error",
                body=f"Unable to read notifications: {str(e)}",
                icon="error",
            ).set_server(self.notifier).show()

    def update_icon(self):
        def count_dir(dir_: NotificationFolder) -> int:
            if self.is_do_not_disturb_active(dir_["path"]):
                return 0
            else:
                return sum(
                    [
                        *map(count_dir, dir_["folders"].values()),
                        *map(len, dir_["notifications"].values()),
                    ]
                )

        file_count = count_dir(self.notification_cache)
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
        pixmap.fill(Qt.transparent)

        # Create a painter
        painter = QPainter(pixmap)

        # Set up the font
        font = QFont()
        font.setPixelSize(24)
        font.setBold(True)
        painter.setFont(font)

        # Draw the red circle
        painter.setBrush(QColor(255, 0, 0))  # Red color
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(0, 0, 40, 40)

        # Set the color for the text (white)
        painter.setPen(QColor(255, 255, 255))

        # Draw the number in the center of the circle
        painter.drawText(pixmap.rect(), Qt.AlignCenter, str(number))

        # End painting
        painter.end()

        # Create and return the icon
        return QIcon(pixmap)

    def set_do_not_disturb(self, folder_path: str, until: datetime):
        libdbus_to_json.do_not_disturb.write_do_not_disturb(
            folder_path, until, cache=self.do_not_disturb
        )
        Notify(
            summary="Do Not Disturb",
            body=f"Do Not Disturb set until {until}",
            icon="dialog-information",
        ).set_server(self.notifier).show()

        self.refresh()

    def get_do_not_disturb(self, folder_path: Path) -> datetime | None:
        return libdbus_to_json.do_not_disturb.get_do_not_disturb(
            folder_path, root_path=self.root_path, cache=self.do_not_disturb
        )

    def is_do_not_disturb_active(self, folder_path: Path) -> bool:
        return libdbus_to_json.do_not_disturb.is_do_not_disturb_active(
            folder_path, root_path=self.root_path, cache=self.do_not_disturb
        )

    def cache_existing_notifications(self):
        for dirpath, _, filenames in os.walk(self.root_path):
            dirpath = Path(dirpath)
            filenames = [f for f in filenames if f.endswith(".json") and f != ".settings.json"]
            if filenames:
                notifications = self.notification_cache
                for folder in dirpath.relative_to(self.root_path).parts:
                    notifications = notifications["folders"].setdefault(
                        folder,
                        NotificationFolder(
                            folders={},
                            notifications={},
                            path=notifications["path"] / folder,
                        ),
                    )
                for filename in filenames:
                    path = dirpath / filename
                    with path.open() as f:
                        notifications["notifications"][filename] = [
                            Notification(
                                json.loads(line)
                                | {
                                    "path": path,
                                    "at": datetime.fromtimestamp(
                                        path.stat().st_mtime, UTC
                                    ),
                                }
                            )
                            for line in f
                        ]

    def trash(self, path: Path):
        notifications = self.notification_cache
        for folder in path.relative_to(self.root_path).parent.parts:
            notifications = notifications["folders"][folder]
        if path.is_file():
            if path.suffix == ".json" and path.name != ".settings.json":
                send2trash(path)
                del notifications["notifications"][path.name]
        else:
            if not list(path.rglob(".settings.json")) and not list(
                path.rglob(".notification.wav")
            ):
                send2trash(path)
                del notifications["folders"][path.name]
            else:
                notifications = notifications["folders"][path.name]
                for folder in notifications["folders"].values():
                    thread = Thread(target=self.trash, args=(folder["path"],))
                    thread.start()
                    self.threads.append(thread)
                for file in notifications["notifications"]:
                    thread = Thread(target=self.trash, args=(path / file,))
                    thread.start()
                    self.threads.append(thread)
        self.refresh()

    def run(self):
        sys.exit(self.app.exec_())


if __name__ == "__main__":
    SystemTrayFileBrowser(Path(sys.argv[1])).run()
