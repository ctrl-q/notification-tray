import logging
from datetime import UTC, datetime
from enum import IntEnum
from pathlib import Path

import dbus
import dbus.decorators
import dbus.service
from notification_tray.utils.logging import log_input_and_output
from notification_tray.utils.paths import get_output_path
from PyQt5.QtCore import QObject, pyqtSignal

from ..types.notification import (CachedNotification, Notification,
                                  NotificationHints)

logger = logging.getLogger(__name__)
VERSION = "0.0.1"


class NotificationCloseReason(IntEnum):
    EXPIRED = 1
    DISMISSED_BY_USER = 2
    CLOSED_BY_CALL_TO_CLOSENOTIFICATION = 3
    UNDEFINED = 4


class NotificationServiceSignaler(QObject):
    notification_ready = pyqtSignal(int)
    notification_closed = pyqtSignal(int, int)


class NotificationService(dbus.service.Object):
    notifications = dict[int, CachedNotification]()

    def __init__(self, root_path: Path):
        bus_name = dbus.service.BusName(
            "org.freedesktop.Notifications", bus=dbus.SessionBus()
        )
        super().__init__(bus_name, "/org/freedesktop/Notifications")
        self.root_path = root_path
        self.signaler = NotificationServiceSignaler()
        logger.info(f"Started notification service with root_path {root_path}")

    @log_input_and_output(logging.INFO)
    @dbus.decorators.signal(  # type: ignore
        dbus_interface="org.freedesktop.Notifications", signature="us"
    )
    def ActionInvoked(self, id: int, action_key: str): ...

    @log_input_and_output(logging.INFO)
    @dbus.decorators.signal(  # type: ignore
        dbus_interface="org.freedesktop.Notifications", signature="sss"
    )
    def NotificationDisplayed(
        self,
        app_name: str,
        summary: str,
        body: str,
    ): ...

    @log_input_and_output(logging.INFO)
    @dbus.decorators.signal(  # type: ignore
        dbus_interface="org.freedesktop.Notifications", signature="uu"
    )
    def NotificationClosed(self, id: int, reason: NotificationCloseReason):
        pass

    @dbus.decorators.method(  # type: ignore
        "org.freedesktop.Notifications",
        in_signature="susssasa{sv}i",
        out_signature="u",
    )
    def Notify(
        self,
        app_name: str,
        replaces_id: int,
        app_icon: str,
        summary: str,
        body: str,
        actions: list[tuple[str, str]],
        hints: NotificationHints,
        expire_timeout: int,
    ):
        # from https://specifications.freedesktop.org/notification-spec/latest/protocol.html: Servers must make sure not to return zero as an ID.
        logger.info(f"Got notification from {app_name} with summary {summary}")
        id = replaces_id or len(self.notifications) + 1
        logger.info(f"Notification ID: {id}")
        notification = Notification(
            app_name=app_name,
            replaces_id=replaces_id,
            app_icon=app_icon,
            summary=summary,
            body=body,
            expire_timeout=expire_timeout,
            id=id,
            actions=actions,
            hints=hints,
            at=datetime.now(UTC),
        )
        self.notifications[id] = CachedNotification(
            **notification, path=get_output_path(self.root_path, notification)
        )
        self.signaler.notification_ready.emit(id)
        logger.info(f"Notification Ready. ID: {id}")
        return id

    @log_input_and_output(logging.INFO)
    @dbus.decorators.method(  # type: ignore
        "org.freedesktop.Notifications",
        in_signature="u",
        out_signature="",
    )
    def CloseNotification(self, id: int):
        if id in self.notifications and not self.notifications[id].get("trashed"):
            reason = NotificationCloseReason.CLOSED_BY_CALL_TO_CLOSENOTIFICATION
            self.NotificationClosed(id, reason)
            self.signaler.notification_closed.emit(id, reason)

        else:
            raise dbus.exceptions.DBusException()

    @dbus.decorators.method("org.freedesktop.Notifications", out_signature="ssss")  # type: ignore
    def GetServerInformation(self) -> tuple[str, str, str, str]:
        return ("notification-tray", "github.com", VERSION, "1.3")

    @dbus.decorators.method("org.freedesktop.Notifications", out_signature="as")  # type: ignore
    def GetCapabilities(self):
        return [
            "action-icons",
            "actions",
            "body",
            "body-hyperlinks",
            "body-images",
            "body-markup",
            "persistence",
            "sound",
        ]
