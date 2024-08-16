import json
import os
import sys
from datetime import datetime, timedelta
from pathlib import Path

from PyQt5.QtCore import QFileSystemWatcher, Qt, QTimer
from PyQt5.QtGui import QColor, QFont, QIcon, QPainter, QPixmap
from PyQt5.QtWidgets import QAction, QApplication, QMenu, QSystemTrayIcon
from send2trash import send2trash


class SystemTrayFileBrowser:
    def __init__(self, root_path: str):
        self.app = QApplication([])
        self.root_path = root_path
        self.tray_icon = None
        self.tray_menu = QMenu()
        self.setup_file_watcher()
        self.start_timer()

        self.update_icon()
        self.setup_tray_menu()

    def setup_file_watcher(self):
        self.watcher = QFileSystemWatcher(
            [self.root_path, *map(str, Path(self.root_path).rglob("*"))]
        )
        self.watcher.directoryChanged.connect(self.on_directory_changed)

    def start_timer(self):
        self.timer = QTimer()
        self.timer.setInterval(60000)  # Check every minute
        self.timer.timeout.connect(self.check_do_not_disturb_status)
        self.timer.start()

    def check_do_not_disturb_status(self):
        # Refresh the tray menu to recheck the status of folders
        self.setup_tray_menu()

    def on_directory_changed(self, path: str):
        self.update_icon()
        self.setup_tray_menu()
        sub_paths = list(map(str, Path(path).rglob("*")))
        if sub_paths:
            self.watcher.addPaths(map(str, sub_paths))

        deleted_paths = [
            path
            for path in self.watcher.directories()
            if not Path(path).absolute().is_dir()
        ]
        if deleted_paths:
            self.watcher.removePaths(map(str, deleted_paths))

    def setup_tray_menu(self):
        self.tray_menu.clear()
        self.add_directory_contents(self.root_path, self.tray_menu)

        # Add exit option
        exit_action = QAction("Exit", self.tray_menu)
        exit_action.triggered.connect(self.app.quit)
        self.tray_menu.addAction(exit_action)

    def add_directory_contents(self, path, menu):
        try:
            if os.path.exists(path):
                items = sorted(os.listdir(path))
                for item in items:
                    item_path = os.path.join(path, item)
                    if os.path.isdir(item_path):
                        if not self.is_do_not_disturb_active(item_path):
                            submenu = QMenu(item, menu)
                            menu.addMenu(submenu)
                            placeholder = QAction("Loading...", submenu)
                            submenu.addAction(placeholder)
                            submenu.aboutToShow.connect(
                                lambda sub=submenu, p=item_path: self.populate_submenu(
                                    sub, p
                                )
                            )
                    else:
                        file_action = QAction(item, menu)
                        file_action.triggered.connect(
                            lambda checked, p=item_path: self.open_file(p)
                        )
                        menu.addAction(file_action)
        except PermissionError:
            error_action = QAction("Permission Denied", menu)
            error_action.setEnabled(False)
            menu.addAction(error_action)

    def populate_submenu(self, submenu, path):
        if submenu.actions()[0].text() == "Loading...":
            submenu.clear()
            self.add_directory_contents(path, submenu)

            # Re-add the "Move to Trash" action after populating
            trash_action = QAction("Move to Trash", submenu)
            trash_action.triggered.connect(lambda checked, p=path: send2trash(p))
            submenu.addAction(trash_action)

            # Add "Do Not Disturb" submenu
            dnd_submenu = QMenu("Do Not Disturb", submenu)
            submenu.addMenu(dnd_submenu)
            for text, duration in [
                ("1 hour", 1),
                ("8 hours", 8),
                ("Forever", None),
            ]:
                dnd_action = QAction(text, dnd_submenu)
                dnd_action.triggered.connect(
                    lambda checked, p=path, d=duration: self.set_do_not_disturb(p, d)
                )
                dnd_submenu.addAction(dnd_action)

    def open_file(self, path: str):
        try:
            content = "\n---\n".join(
                map(
                    lambda d: d["body"],
                    map(json.loads, Path(path).read_text().splitlines()),
                )
            )
            # Truncate content if it's too long
            if len(content) == 1000:
                content = content[:997] + "..."

            # Show notification with file contents
            self.tray_icon.showMessage(
                "notification-tray",
                content,
                QSystemTrayIcon.Information,
                5000,  # Display for 5 seconds
            )
        except Exception as e:
            self.tray_icon.showMessage(
                "Error", f"Unable to read file: {str(e)}", QSystemTrayIcon.Warning, 3000
            )

    def update_icon(self):
        def count_dir(dir_: Path) -> int:
            if self.is_do_not_disturb_active(str(dir_)):
                return 0
            else:
                count = 0
                for item in dir_.iterdir():
                    item = item.resolve()
                    if item.is_dir():
                        count += count_dir(item)
                    else:
                        count += 1
                return count


        file_count = count_dir(Path(self.root_path))
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

    def set_do_not_disturb(self, folder_path: str, hours: int | None):
        (Path(folder_path) / ".settings.json").write_text(
            json.dumps(
                {
                    "do_not_disturb_until": (
                        None
                        if hours is None
                        else (datetime.utcnow() + timedelta(hours=hours)).isoformat()
                    )
                }
            )
        )
        self.tray_icon.showMessage(
            "Do Not Disturb",
            f"Do Not Disturb set for {'forever' if not hours else f'{hours} hour(s)'}",
            QSystemTrayIcon.Information,
            3000,
        )

        # Refresh tray menu to hide the folder if necessary
        self.setup_tray_menu()

    def is_do_not_disturb_active(self, folder_path: str) -> bool:
        settings_file = Path(folder_path) / ".settings.json"
        if settings_file.exists():
            settings = json.loads(settings_file.read_text())
            return "do_not_disturb_until" in settings and (
                settings["do_not_disturb_until"] is None
                or datetime.fromisoformat(settings["do_not_disturb_until"])
                > datetime.now()
            )

    def run(self):
        sys.exit(self.app.exec_())


if __name__ == "__main__":
    SystemTrayFileBrowser(sys.argv[1]).run()
