from datetime import datetime
from pathlib import Path

import libdbus_to_json.do_not_disturb


def get_notification_backoff_minutes(
    root_path: Path, folder_path: Path, cache: dict[Path, int]
) -> int:
    for folder in [
        folder_path,
        *map(
            root_path.joinpath,
            folder_path.relative_to(root_path).parents,
        ),
    ]:
        if folder in cache:
            return cache[folder]
    return 0


def is_do_not_disturb_active(
    root_path: Path, folder_path: Path, cache: libdbus_to_json.do_not_disturb.Cache
) -> bool:
    return libdbus_to_json.do_not_disturb.is_datetime_setting_active(
        folder_path, root_path=root_path, cache=cache
    )


def get_do_not_disturb(
    root_path: Path, folder_path: Path, cache: libdbus_to_json.do_not_disturb.Cache
) -> datetime | None:
    return libdbus_to_json.do_not_disturb.get_datetime_setting(
        folder_path, root_path=root_path, cache=cache
    )


def is_hide_from_tray_active(
    root_path: Path, folder_path: Path, cache: libdbus_to_json.do_not_disturb.Cache
) -> bool:
    return libdbus_to_json.do_not_disturb.is_datetime_setting_active(
        folder_path, root_path=root_path, cache=cache
    )
