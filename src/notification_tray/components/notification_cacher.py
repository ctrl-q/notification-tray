import json
import logging
from datetime import UTC, datetime
from pathlib import Path
from threading import Thread

from PyQt5.QtCore import QObject, pyqtSignal  # type: ignore
from send2trash import send2trash as _send2trash

from notification_tray.components.notifier import Notifier
from notification_tray.types.notification import (CachedNotification,
                                                  NotificationFolder)
from notification_tray.utils import settings
from notification_tray.utils.logging import log_input_and_output

send2trash = log_input_and_output(logging.INFO)(_send2trash)

logger = logging.getLogger(__name__)


class NotificationCacher(QObject):
    notifications_cached = pyqtSignal()

    def __init__(
        self,
        root_path: Path,
        notifier: Notifier,
        do_not_disturb: settings.Cache,
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
        logger.info(f"Started notification cacher with root path {root_path}")

    def cache_existing_notifications(self, root_path: Path):
        logger.info(f"Caching existing notifications under {root_path}")
        for dirpath, _, filenames in root_path.walk():
            dirpath = Path(dirpath)
            filenames = [
                f for f in filenames if f.endswith(".json") and f != ".settings.json"
            ]
            if filenames:
                logger.info(f"Found {len(filenames)} notifications under {dirpath}")
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
                    try:
                        logger.debug(f"Loading {path}")
                        notifications["notifications"][filename] = CachedNotification(
                            json.loads(path.read_text())
                            | {
                                "path": path,
                                "at": datetime.fromtimestamp(path.stat().st_mtime, UTC),
                            }
                        )
                    except Exception as e:
                        logging.exception(f"Got exception at file {path}: {e}")
                        raise
        self.notifications_cached.emit()

    def cache(self, notification: CachedNotification):
        notifications = self.notification_cache
        if not notification["hints"].get("transient"):
            notification["path"].parent.mkdir(parents=True, exist_ok=True)
            notification_to_dump = dict(notification.copy())
            del notification_to_dump["at"]
            del notification_to_dump["path"]
            with notification["path"].open("w") as f:
                json.dump(notification_to_dump, f)
            logging.info(
                f"Notification {notification['summary']} written to {notification['path']}",
            )

        for folder in (
            notification["path"]
            .relative_to(self.notification_cache["path"])
            .parent.parts
        ):
            notifications = notifications["folders"].setdefault(
                folder,
                NotificationFolder(
                    folders={}, notifications={}, path=notifications["path"] / folder
                ),
            )
        path = notification["path"]
        notifications["notifications"][path.name] = notification
        self.notifications_cached.emit()

    @log_input_and_output(logging.INFO)
    def trash(self, path: Path):
        def mark_as_trashed(folder: NotificationFolder):
            for notification in folder["notifications"].values():
                notification["trashed"] = True
            for subfolder in folder["folders"].values():
                mark_as_trashed(subfolder)

        if path.exists():
            notifications = self.notification_cache
            for folder in path.relative_to(self.root_path).parent.parts:
                notifications = notifications["folders"][folder]
            if path.is_file():
                if path.suffix == ".json" and path.name != ".settings.json":
                    send2trash(path)
                    notifications["notifications"][path.name]["trashed"] = True
            else:
                if not list(path.rglob(".settings.json")) and not list(
                    path.rglob(".notification.wav")
                ):
                    send2trash(path)
                    mark_as_trashed(notifications["folders"][path.name])
                else:
                    if path != self.root_path:
                        notifications = notifications["folders"][path.name]
                    for folder in notifications["folders"].values():
                        Thread(
                            target=self.trash, args=(folder["path"],), daemon=True
                        ).start()
                    for file in notifications["notifications"]:
                        Thread(
                            target=self.trash, args=(path / file,), daemon=True
                        ).start()

            self.notifications_cached.emit()
        else:
            logger.info(f"Path does not exist. Skipping {path}")
