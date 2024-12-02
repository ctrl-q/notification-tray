import json
from pathlib import Path

import libdbus_to_json
import libdbus_to_json.do_not_disturb
from PyQt5.QtCore import QFileSystemWatcher


class FileWatcher:
    def __init__(
        self,
        root_path: Path,
        do_not_disturb: libdbus_to_json.do_not_disturb.Cache,
        hide_from_tray: libdbus_to_json.do_not_disturb.Cache,
        notification_backoff_minutes: dict[Path, int],
    ) -> None:
        self.do_not_disturb = do_not_disturb
        self.hide_from_tray = hide_from_tray
        self.notification_backoff_minutes = notification_backoff_minutes
        self.watcher = QFileSystemWatcher(
            [
                str(root_path),
                *map(
                    str,
                    root_path.rglob(".settings.json"),
                ),
            ]
        )
        self.watcher.fileChanged.connect(self.on_settings_file_changed)
        for settings_file in self.watcher.files():
            self.on_settings_file_changed(settings_file)

    def on_settings_file_changed(self, path: str):
        path_ = Path(path)
        if path_.parent in self.do_not_disturb:
            del self.do_not_disturb[path_.parent]
        try:
            for setting_name, cache in [
                ("do_not_disturb_until", self.do_not_disturb),
                ("hide_from_tray_until", self.hide_from_tray),
            ]:
                libdbus_to_json.do_not_disturb.cache_datetime_setting(
                    path_.parent, setting_name, cache=cache
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
