import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from threading import Thread

import libdbus_to_json.do_not_disturb
from notification_tray.components.notifier import Notifier
from notification_tray.types.notification import (Notification,
                                                  NotificationFolder)
from notification_tray.utils.settings import (get_notification_backoff_minutes,
                                              is_do_not_disturb_active)
from PyQt5.QtCore import QObject, pyqtSignal, pyqtSlot  # type: ignore
from PyQt5.QtDBus import QDBusConnection
from send2trash import send2trash


class NotificationCacher(QObject):
    notification_cached = pyqtSignal()

    def __init__(
        self,
        root_path: Path,
        notifier: Notifier,
        do_not_disturb: libdbus_to_json.do_not_disturb.Cache,
        notification_backoff_minutes: dict[Path, int],
        notification_cache: NotificationFolder,
        parent: QObject | None = None,
    ):
        super().__init__(parent)
        self.root_path = root_path
        self.notifier = notifier
        self.notification_cache = notification_cache
        self.do_not_disturb = do_not_disturb
        self.notification_backoff_minutes = notification_backoff_minutes
        QDBusConnection.sessionBus().connect(  # type: ignore
            "",
            "/com/example/DbusNotificationsToJson/notifications",
            "com.example.DbusNotificationsToJson",
            "NotificationSent",
            self.cache,
        )

        self.cache_existing_notifications(root_path)

    def cache_existing_notifications(self, root_path: Path):
        for dirpath, _, filenames in os.walk(root_path):
            dirpath = Path(dirpath)
            filenames = [
                f for f in filenames if f.endswith(".json") and f != ".settings.json"
            ]
            if filenames:
                notifications = self.notification_cache
                for folder in dirpath.relative_to(root_path).parts:
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
                    notifications["notifications"][filename] = Notification(
                        json.loads(path.read_text())
                        | {
                            "path": path,
                            "at": datetime.fromtimestamp(path.stat().st_mtime, UTC),
                        }
                    )
        self.notification_cached.emit()

    @pyqtSlot(str, int, str, str, str, int, int, str)
    def cache(
        self,
        app_name: str,
        replaces_id: int,
        app_icon: str,
        summary: str,
        body: str,
        expire_timeout: int,
        id: int,
        path: str,
    ):
        notification = Notification(
            app_name=app_name,
            replaces_id=replaces_id,
            app_icon=app_icon,
            summary=summary,
            body=body,
            expire_timeout=expire_timeout,
            id=id,
            path=path,
            at=datetime.now(UTC),
        )
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
        path_ = Path(notification["path"])
        notifications["notifications"][path_.name] = notification
        if is_do_not_disturb_active(
            self.root_path, path_.parent, self.do_not_disturb
        ) or get_notification_backoff_minutes(
            self.root_path, path_.parent, self.notification_backoff_minutes
        ):
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
            self.notifier.notify([notification])
        self.notification_cached.emit()

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
                    Thread(
                        target=self.trash, args=(folder["path"],), daemon=True
                    ).start()
                for file in notifications["notifications"]:
                    Thread(target=self.trash, args=(path / file,), daemon=True).start()
        self.notification_cached.emit()
