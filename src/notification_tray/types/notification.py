from datetime import datetime
from pathlib import Path
from typing import Literal, TypedDict

from typing_extensions import NotRequired

# TODO (med) switch to pydantic
NotificationHints = TypedDict(
    "NotificationHints",
    {
        "action-icons": NotRequired[bool],
        "category": NotRequired[str],
        "desktop-entry": NotRequired[str],
        "icon_data": NotRequired[tuple[int, int, int, bool, int, int, bytearray]],
        "image-data": NotRequired[tuple[int, int, int, bool, int, int, bytearray]],
        "image_data": NotRequired[tuple[int, int, int, bool, int, int, bytearray]],
        "image-path": NotRequired[str],
        "image_path": NotRequired[str],
        "sound-file": NotRequired[str],
        "sound-name": NotRequired[str],
        "suppress-sound": NotRequired[bool],
        "resident": NotRequired[bool],
        "transient": NotRequired[bool],
        "urgency": NotRequired[Literal[0, 1, 2]],
        "x": NotRequired[int],
        "y": NotRequired[int],
    },
)


class Notification(TypedDict):
    app_name: str
    replaces_id: int
    app_icon: str
    summary: str
    body: str
    actions: list[tuple[str, str]]
    hints: NotificationHints
    expire_timeout: int
    id: int
    at: datetime


class CachedNotification(Notification):
    path: Path


class NotificationFolder(TypedDict):
    folders: dict[str, "NotificationFolder"]
    notifications: dict[str, CachedNotification]
    path: Path
