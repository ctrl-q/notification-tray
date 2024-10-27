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
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime, timedelta
from pathlib import Path

import libdbus_to_json
import libdbus_to_json.do_not_disturb
from desktop_notify import BaseNotify, glib
from pydub import AudioSegment
from pydub.playback import play
from PyQt5.QtCore import QFileSystemWatcher, Qt, QTimer
from PyQt5.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PyQt5.QtWidgets import QAction, QApplication, QMenu, QSystemTrayIcon
from send2trash import send2trash


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


class SystemTrayFileBrowser:
    def __init__(self, root_path: Path):
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
        self.setup_file_watcher()
        self.start_timer()
        self.update_icon()
        self.setup_tray_menu()

    def setup_file_watcher(self):
        self.watcher = QFileSystemWatcher(
            [
                str(self.root_path),
                *map(
                    str,
                    filter(
                        lambda path: path.is_dir() or path.name == ".settings.json",
                        self.root_path.rglob("*"),
                    ),
                ),
            ]
        )
        self.watcher.fileChanged.connect(self.on_settings_file_changed)
        for settings_file in self.watcher.files():
            self.on_settings_file_changed(settings_file)
        self.watcher.directoryChanged.connect(self.on_directory_changed)

    def start_timer(self):
        self.timer = QTimer()
        self.timer.setInterval(60000)  # Check every minute
        self.timer.timeout.connect(self.check_do_not_disturb_status)
        self.timer.timeout.connect(self.batch_notify)
        self.timer.start()

    def check_do_not_disturb_status(self):
        # Refresh the tray menu to recheck the status of folders
        self.setup_tray_menu()

    def batch_notify(self):
        for parent, _, files in os.walk(self.root_path):
            parent = Path(parent)
            new_notifications: list[Path] = []
            for file in files:
                file = parent / file
                if (
                    file.suffix == (".jsonl")
                    and not self.is_do_not_disturb_active(parent)
                    and (
                        notification_backoff_minutes := self.get_notification_backoff_minutes(
                            parent
                        )
                        > 0
                    )
                    and (
                        datetime.now(UTC)
                        - (datetime.fromtimestamp(os.stat(parent / file).st_mtime, UTC))
                    ).total_seconds()
                    / 60
                    <= notification_backoff_minutes
                ):
                    new_notifications.append(file)

            if new_notifications:
                self.open_files(new_notifications, expires_in_milliseconds=9999999)

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

    def on_directory_changed(self, path: str):
        self.update_icon()
        self.setup_tray_menu()
        for parent, directories, files in os.walk(path):
            if directories:
                self.watcher.addPaths(
                    (os.path.join(parent, directory) for directory in directories)
                )
            parent = Path(parent)
            max_id = self.last_notified.setdefault(parent, -1)
            # Notify individually for every notification since last_notified
            for file in files:
                file = parent / file
                if (
                    file.suffix == (".jsonl")
                    and not self.is_do_not_disturb_active(parent)
                    and self.get_notification_backoff_minutes(parent) == 0
                    and datetime.fromtimestamp(file.stat().st_mtime, UTC)
                    >= self.started_at
                    and (id := int(file.stem.split("-").pop()))
                    > self.last_notified[parent]
                ):
                    self.open_files([file])
                    max_id = max(id, max_id)

            self.last_notified[parent] = max(self.last_notified[parent], max_id)

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
        try:
            if path.exists():
                items = sorted(map(path.joinpath, path.iterdir()))
                for item in items:
                    if item.is_dir():
                        if not self.is_do_not_disturb_active(item):
                            submenu = QMenu(item.name, menu)
                            menu.addMenu(submenu)
                            placeholder = QAction("Loading...", submenu)
                            submenu.addAction(placeholder)
                            submenu.aboutToShow.connect(
                                lambda sub=submenu, p=item: self.populate_submenu(
                                    sub, p
                                )
                            )
                    elif item.name == ".settings.json":
                        self.watcher.addPath(item.name)
                    elif item.name == ".notification.wav":
                        self.notification_sounds.add(item)
                    else:
                        file_action = QAction(item.name, menu)
                        file_action.triggered.connect(
                            lambda checked, p=item: self.open_files([p])
                        )
                        menu.addAction(file_action)
        except PermissionError:
            error_action = QAction("Permission Denied", menu)
            error_action.setEnabled(False)
            menu.addAction(error_action)

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

    def open_files(self, paths: list[Path], expires_in_milliseconds: int = 5000):
        try:
            json_lines: list[dict[str, str]] = []
            for path in paths:
                for line in path.read_text().splitlines():
                    json_lines.append(json.loads(line))
            if json_lines:
                app_name = json_lines[-1]["app_name"]
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
                        json_lines,
                    ),
                )
                # Truncate content if it's too long
                if len(content) >= 1000:
                    content = content[:997] + "..."

                self.play_notification_sound(paths[-1].parent)
                Notify(
                    summary=(
                        app_name
                        if len(json_lines) == 1
                        else f"{len(json_lines)} new notifications from {app_name}"
                    ),
                    body=content,
                    timeout=max(
                        *(int(d.get("expire_timeout", -1)) for d in json_lines),
                        expires_in_milliseconds,
                    ),
                ).set_id(json_lines[-1]["id"]).set_server(self.notifier).show()

        except Exception as e:
            self.play_notification_sound(paths[-1].parent)
            Notify(
                summary="Error",
                body=f"Unable to read files: {str(e)}",
                icon="error",
            ).set_server(self.notifier).show()

    def update_icon(self):
        def count_dir(dir_: Path) -> int:
            if self.is_do_not_disturb_active(dir_):
                return 0
            else:
                count = 0
                for item in dir_.iterdir():
                    item = item.resolve()
                    if item.is_dir():
                        count += count_dir(item)
                    elif item.suffix == ".jsonl":
                        count += 1
                return count

        file_count = count_dir(self.root_path)
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

        # Refresh tray menu to hide the folder if necessary
        self.setup_tray_menu()

    def get_do_not_disturb(self, folder_path: Path) -> datetime | None:
        return libdbus_to_json.do_not_disturb.get_do_not_disturb(
            folder_path, root_path=self.root_path, cache=self.do_not_disturb
        )

    def is_do_not_disturb_active(self, folder_path: Path) -> bool:
        return libdbus_to_json.do_not_disturb.is_do_not_disturb_active(
            folder_path, root_path=self.root_path, cache=self.do_not_disturb
        )

    def trash(self, path: Path):
        protected_files = {".settings.json", ".notification.wav"}
        if path.is_file():
            if path.name not in protected_files:
                send2trash(path)
        elif any(
            protected_files.issubset(filenames) for _, _, filenames in os.walk(path)
        ):
            with ThreadPoolExecutor() as pool:
                pool.map(self.trash, path.iterdir())
        else:
            send2trash(path)

    def run(self):
        sys.exit(self.app.exec_())


if __name__ == "__main__":
    SystemTrayFileBrowser(Path(sys.argv[1])).run()
