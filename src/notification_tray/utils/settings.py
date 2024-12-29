import json
import logging
from datetime import UTC, datetime
from pathlib import Path

from notification_tray.utils.logging import log_input_and_output

DoNotDisturb = datetime | None
Cache = dict[Path, DoNotDisturb]


@log_input_and_output(logging.DEBUG)
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


@log_input_and_output(logging.DEBUG)
def is_do_not_disturb_active(root_path: Path, folder_path: Path, cache: Cache) -> bool:
    return is_datetime_setting_active(folder_path, root_path=root_path, cache=cache)


@log_input_and_output(logging.DEBUG)
def get_do_not_disturb(
    root_path: Path, folder_path: Path, cache: Cache
) -> datetime | None:
    return get_datetime_setting(folder_path, root_path=root_path, cache=cache)


@log_input_and_output(logging.DEBUG)
def is_hide_from_tray_active(root_path: Path, folder_path: Path, cache: Cache) -> bool:
    return is_datetime_setting_active(folder_path, root_path=root_path, cache=cache)


# TODO refactor to accept any type of setting, not just datetime
def cache_datetime_setting(folder_path: Path, setting_name: str, *, cache: Cache):
    settings_file = folder_path / ".settings.json"

    try:
        settings = json.loads(settings_file.read_text())
    except FileNotFoundError:
        return
    else:
        if setting_name in settings:
            cache[folder_path] = settings[setting_name] and datetime.fromisoformat(
                settings[setting_name]
            )


# TODO refactor to accept any type of setting, not just datetime
def get_datetime_setting(
    folder_path: Path, *, root_path: Path, cache: Cache
) -> datetime | None:
    for folder in [
        folder_path,
        *map(
            root_path.joinpath,
            folder_path.relative_to(root_path).parents,
        ),
    ]:
        if folder in cache:
            return cache[folder]


def is_datetime_setting_active(
    folder_path: Path, *, root_path: Path, cache: Cache
) -> bool:
    return (
        datetime_ := get_datetime_setting(folder_path, root_path=root_path, cache=cache)
        or False
    ) and datetime_ > datetime.now(UTC)


# TODO refactor to accept any type of setting, not just datetime
def write_datetime_setting(
    folder_path: str, setting_name: str, until: datetime, *, cache: Cache
):
    folder_path_ = Path(folder_path).absolute()
    settings_file = folder_path_ / ".settings.json"
    cache[folder_path_] = until
    try:
        existing_settings = json.loads(settings_file.read_text())
    except FileNotFoundError:
        existing_settings = {}
    settings_file.write_text(
        json.dumps(existing_settings | {setting_name: until.isoformat()})
    )
