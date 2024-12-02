from concurrent.futures import ThreadPoolExecutor
from datetime import UTC, datetime
from pathlib import Path

import libdbus_to_json
import libdbus_to_json.do_not_disturb
from desktop_notify import glib
from desktop_notify.notify import Notify as BaseNotify
from notification_tray.types.notification import (Notification,
                                                  NotificationFolder)
from notification_tray.utils.settings import (get_do_not_disturb,
                                              get_notification_backoff_minutes,
                                              is_do_not_disturb_active)
from pydub.audio_segment import AudioSegment
from pydub.playback import play  # type: ignore


# the Notify from desktop_notify.glib incorrectly subclasses BaseServer, so I have to re-define it myself
class Notify(BaseNotify):

    def show(self):
        self.__id = self.server.show(self) # type: ignore

    def show_sync(self):
        self.__id = self.server.show_sync(self) # type: ignore

    def close(self):
        if self.__id: # type: ignore
            self.server.close(self) # type: ignore

    def close_sync(self):
        if self.__id: # type: ignore
            self.server.close_sync(self) # type: ignore

    def set_server(self, server: glib.Server): # type: ignore
        return super().set_server(server) # type: ignore

    @property
    def server_class(self): # type: ignore
        return glib.Server # type: ignore


class Notifier:
    notifier = Notify()
    server = glib.server.Server("notification-tray")
    notification_sounds: set[Path] = set()
    last_notified: dict[Path, int] = {}
    started_at = datetime.now(UTC)

    def __init__(
        self,
        root_path: Path,
        do_not_disturb: libdbus_to_json.do_not_disturb.Cache,
        notification_backoff_minutes: dict[Path, int],
        notification_cache: NotificationFolder,
    ) -> None:
        self.root_path = root_path
        self.do_not_disturb = do_not_disturb
        self.notification_backoff_minutes = notification_backoff_minutes
        self.notification_cache = notification_cache

    def notify(
        self, notifications: list[Notification], expires_in_milliseconds: int = 5000
    ) -> None:
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
                ).set_id(notifications[-1]["id"]).set_server(self.server).show() # type: ignore
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
            ).set_server(self.server).show() # type: ignore

    def batch_notify(self):
        def process(folder: NotificationFolder):
            self.last_notified.setdefault(folder["path"], -1)
            new_notifications: list[Notification] = []
            for notification in folder["notifications"].values():
                at = notification["at"]
                id = notification["id"]
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
                    new_notifications.append(notification)

            if new_notifications:
                self.notify(
                    new_notifications,
                    expires_in_milliseconds=9999999,
                )
            with ThreadPoolExecutor() as pool:
                pool.map(process, folder["folders"].values())

        process(self.notification_cache)

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
                play(AudioSegment.from_file(notification_sound)) # type: ignore
