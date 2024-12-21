# TODO (low) stop using libdbus-to-json
# TODO (med) use watchdog for file and setting monitoring
import json
import logging
import os
import signal
import sys
from functools import partial
from pathlib import Path
from typing import Any

import libdbus_to_json.do_not_disturb
from dbus.mainloop.glib import DBusGMainLoop  # type: ignore
from PyQt5.QtCore import QTimer, pyqtSignal
from PyQt5.QtGui import QIcon
from PyQt5.QtWidgets import QApplication

from notification_tray.components.notification_cacher import NotificationCacher
from notification_tray.components.notification_service import (
    NotificationCloseReason, NotificationService)
from notification_tray.components.notifier import Notifier
from notification_tray.components.tray import Tray
from notification_tray.types.notification import NotificationFolder
from notification_tray.utils.fp import compose

logging.basicConfig(
    level=os.getenv("LOGLEVEL"),
    format="{asctime} [{levelname}] {name}: {message}",
    style="{",
    stream=sys.stdout,
)
logger = logging.getLogger(__name__)

signal.signal(signal.SIGINT, signal.SIG_DFL)


# TODO use https://git.nullroute.lt/hacks/dzenify.git/tree/dzenify for reference
class SystemTrayFileBrowser(QApplication):
    application_started = pyqtSignal()

    def __init__(self, root_path: Path):
        super().__init__([])
        logger.info(f"Starting application with root path {root_path}")
        self.root_path = root_path
        self.refresh_settings()
        self.notification_service = NotificationService(self.root_path)

        QIcon.setThemeName("Papirus-Dark")  # TODO load the LXQt theme
        # # TODO load the LXQt theme
        with open("/usr/share/lxqt/themes/less/lxqt-notificationd.qss") as f:
            self.setStyleSheet(
                f.read()
                .replace("url(./", "url(")
                .replace("url(", "url(/usr/share/lxqt/themes/less/")
                .replace("Notification", "NotificationWidget")
            )
        self.notification_cache = NotificationFolder(
            folders={}, notifications={}, path=root_path
        )
        self.notifier = Notifier(
            self.root_path,
            self.do_not_disturb,
            notification_backoff_minutes=self.notification_backoff_minutes,
            notification_cache=self.notification_cache,
            parent=self,
        )
        self.notification_cacher = NotificationCacher(
            notification_cache=self.notification_cache,
            root_path=self.root_path,
            notifier=self.notifier,
            notification_backoff_minutes=self.notification_backoff_minutes,
            do_not_disturb=self.do_not_disturb,
            parent=self,
        )
        self.notification_service.signaler.notification_ready.connect(
            compose(
                self.notification_service.notifications.__getitem__,
                self.notification_cacher.cache,
            )
        )
        self.notification_service.signaler.notification_ready.connect(
            compose(
                self.notification_service.notifications.__getitem__,
                self.notifier.notify,
            )
        )
        self.notifier.notification_closed.connect(self.close_if_in_this_run)
        self.notifier.notification_closed.connect(self.trash_if_closed)
        self.notifier.action_invoked.connect(self.notification_service.ActionInvoked)

        self.tray = Tray(
            app=self,
            notifier=self.notifier,
            root_path=self.root_path,
            do_not_disturb=self.do_not_disturb,
            notification_cacher=self.notification_cacher,
            hide_from_tray=self.hide_from_tray,
            notification_backoff_minutes=self.notification_backoff_minutes,
        )
        self.notification_cacher.notifications_cached.connect(self.tray.refresh)
        self.start_timer()

        self.application_started.connect(
            partial(
                self.notification_cacher.cache_existing_notifications, self.root_path
            )
        )
        self.application_started.emit()

    def start_timer(self):
        self.timer = QTimer()
        self.timer.setInterval(60000)
        self.timer.timeout.connect(self.refresh_settings)
        self.timer.timeout.connect(self.tray.refresh)
        self.timer.timeout.connect(self.notifier.batch_notify)
        self.timer.start()

    def refresh_settings(
        self,
    ) -> None:
        do_not_disturb = libdbus_to_json.do_not_disturb.Cache()
        hide_from_tray = libdbus_to_json.do_not_disturb.Cache()
        notification_backoff_minutes = dict[Path, int]()
        for settings_file in Path(self.root_path).rglob(".settings.json"):
            try:
                for setting_name, cache in [
                    ("do_not_disturb_until", do_not_disturb),
                    ("hide_from_tray_until", hide_from_tray),
                ]:
                    libdbus_to_json.do_not_disturb.cache_datetime_setting(
                        settings_file.parent, setting_name, cache=cache
                    )
                if (
                    notification_backoff_minutes_ := json.loads(
                        settings_file.read_text()
                    ).get("notification_backoff_minutes")
                ) is not None:
                    notification_backoff_minutes[settings_file.parent] = int(
                        notification_backoff_minutes_
                    )
            except FileNotFoundError:
                pass
        self.do_not_disturb = do_not_disturb
        self.hide_from_tray = hide_from_tray
        self.notification_backoff_minutes = notification_backoff_minutes

    def close_if_in_this_run(
        self, id: int, reason: NotificationCloseReason, _: Any, is_in_this_run: bool
    ):
        if is_in_this_run:
            self.notification_service.NotificationClosed(id, reason)

    def trash_if_closed(
        self, _: Any, reason: NotificationCloseReason, path: str, __: Any
    ):
        if NotificationCloseReason(reason) in [
            NotificationCloseReason.CLOSED_BY_CALL_TO_CLOSENOTIFICATION,
            NotificationCloseReason.DISMISSED_BY_USER,
        ]:
            self.notification_cacher.trash(Path(path))


def main():
    DBusGMainLoop(set_as_default=True)
    sys.exit(SystemTrayFileBrowser(Path(sys.argv[1])).exec_())


if __name__ == "__main__":
    main()
