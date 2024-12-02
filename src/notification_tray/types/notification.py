from datetime import datetime
from pathlib import Path
from typing import TypedDict


class Notification(TypedDict):
    app_name: str
    replaces_id: int
    app_icon: str
    summary: str
    body: str
    expire_timeout: int
    id: int
    path: str
    at: datetime


class NotificationFolder(TypedDict):
    folders: dict[str, "NotificationFolder"]
    notifications: dict[str, Notification]
    path: Path
