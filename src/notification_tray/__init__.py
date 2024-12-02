import sys
from datetime import datetime
from pathlib import Path

from notification_tray.components.file_watcher import FileWatcher
from notification_tray.components.notification_cacher import NotificationCacher
from notification_tray.components.notifier import Notifier
from notification_tray.components.tray import Tray
from notification_tray.types.notification import NotificationFolder
from PyQt5.QtCore import QTimer
from PyQt5.QtWidgets import QApplication


class SystemTrayFileBrowser:
    def __init__(self, root_path: Path):
        self.app = QApplication([])
        self.root_path = root_path
        self.do_not_disturb: dict[Path, datetime | None] = {}
        self.hide_from_tray: dict[Path, datetime | None] = {}
        self.notification_backoff_minutes: dict[Path, int] = {}
        self.notification_cache = NotificationFolder(
            folders={}, notifications={}, path=root_path
        )
        FileWatcher(
            self.root_path,
            self.do_not_disturb,
            self.hide_from_tray,
            self.notification_backoff_minutes,
        )
        self.notifier = Notifier(
            self.root_path,
            self.do_not_disturb,
            notification_backoff_minutes=self.notification_backoff_minutes,
            notification_cache=self.notification_cache,
        )
        self.notification_cacher = NotificationCacher(
            notification_cache=self.notification_cache,
            root_path=self.root_path,
            notifier=self.notifier,
            notification_backoff_minutes=self.notification_backoff_minutes,
            do_not_disturb=self.do_not_disturb,
            parent=self.app,
        )
        self.tray = Tray(
            app=self.app,
            notifier=self.notifier,
            root_path=self.root_path,
            do_not_disturb=self.do_not_disturb,
            notification_cacher=self.notification_cacher,
            hide_from_tray=self.hide_from_tray,
        )
        self.start_timer()

    def start_timer(self):
        self.timer = QTimer()
        self.timer.setInterval(60000)  # Check every minute
        self.timer.timeout.connect(self.tray.refresh)
        self.timer.timeout.connect(self.notifier.batch_notify)
        self.timer.start()

    def run(self):
        sys.exit(self.app.exec_())


def main():
    SystemTrayFileBrowser(Path(sys.argv[1])).run()


if __name__ == "__main__":
    main()
