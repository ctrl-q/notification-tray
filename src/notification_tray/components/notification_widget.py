import logging

from PyQt5.QtCore import Qt, QTimer, pyqtSignal
from PyQt5.QtGui import QIcon, QImage, QMouseEvent, QPixmap
from PyQt5.QtWidgets import (QHBoxLayout, QLabel, QPushButton, QVBoxLayout,
                             QWidget)

from ..types.notification import CachedNotification
from .notification_service import NotificationCloseReason

logger = logging.getLogger(__name__)


class NotificationWidget(QWidget):
    action_invoked = pyqtSignal(str)
    displayed = pyqtSignal()
    closed = pyqtSignal(int)

    def __init__(self, data: CachedNotification):
        super().__init__()
        self.data = data
        self.action_invoked.connect(
            lambda: (
                self.closed.emit(NotificationCloseReason.DISMISSED_BY_USER)
                if not self.data.get("resident")
                else None
            )
        )
        self.was_displayed = False
        self.displayed.connect(self._set_was_displayed)
        self.initUI()

    def mousePressEvent(self, a0: QMouseEvent | None) -> None:
        actions = self.data["actions"]
        default_action = (
            next(iter(actions))
            if len(actions) == 1
            else "default" if "default" in actions else None
        )
        if default_action:
            self.action_invoked.emit(default_action)
        return super().mousePressEvent(a0)

    def _set_was_displayed(self):
        self.was_displayed = True

    def _get_icon(self) -> QIcon | None:
        if image_data := (
            self.data.get("hints", {}).get("image-data")
            or self.data.get("hints", {}).get("image_data")
            or self.data.get("hints", {}).get("icon_data")
        ):
            logger.debug("Getting notification icon from hints")
            width, height, rowstride, has_alpha, bits_per_sample, channels, data = (
                image_data
            )
            assert bits_per_sample == 8
            assert channels == 4 if has_alpha else 3
            return QIcon(
                QPixmap.fromImage(
                    QImage(
                        bytes(data),
                        width,
                        height,
                        rowstride,
                        (
                            QImage.Format.Format_RGBA8888
                            if has_alpha
                            else QImage.Format.Format_RGB888
                        ),
                    )
                )
            )

        elif label := (
            self.data.get("hints", {}).get("image-path")
            or self.data.get("hints", {}).get("image_path")
            or self.data["app_icon"]
        ):
            if label.startswith("file://"):
                logger.debug(f"Getting notification icon from path {label}")
                return QIcon(label.removesuffix("file://"))
            else:
                logger.debug(
                    f"Getting notification icon from theme with icon name {label}"
                )
                return QIcon.fromTheme(label)

    def initUI(self):
        logger.info("Creating notification widget")
        self.setFixedWidth(256)
        # TODO (high) error handling
        self.setWindowFlags(
            Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.ToolTip  # type: ignore
        )
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)

        layout = QVBoxLayout()
        top_layout = QHBoxLayout()

        logger.debug("Getting icon")
        icon = self._get_icon()
        if icon:
            logger.debug("Got icon")
            icon_label = QLabel(self)
            icon_label.setAlignment(Qt.AlignmentFlag.AlignLeft)
            icon_label.setPixmap(icon.pixmap(32, 32))
            top_layout.addWidget(icon_label)
        else:
            logger.debug("No icon")

        app_name_label = QLabel(self.data["app_name"], self)
        app_name_label.setObjectName("appLabel")
        top_layout.addWidget(app_name_label)

        close_button = QPushButton()
        close_button.setObjectName(
            "closeButton"
        )  # for compatibility with lxqt-notificationd themes
        close_button.clicked.connect(
            lambda: self.closed.emit(NotificationCloseReason.DISMISSED_BY_USER.value)
        )

        top_layout.addWidget(close_button)
        layout.addLayout(top_layout)
        summary_layout = QVBoxLayout()
        summary_label = QLabel(self.data["summary"] + "\n" + self.data["body"], self)
        summary_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        summary_label.setWordWrap(True)
        summary_layout.addWidget(summary_label)
        layout.addLayout(summary_layout)

        button_layout = QHBoxLayout()

        logger.debug("Adding action buttons")
        for key, value in self.data["actions"].items():
            button = QPushButton(None, self)
            if self.data.get("hints", {}).get("action-icons"):
                button.setIcon(QIcon.fromTheme(value))
            else:
                button.setText(value)
            button.clicked.connect(lambda: self.action_invoked.emit(key))
            logger.debug(f"Adding action button {key} => {value}")
            button_layout.addWidget(button)

        layout.addLayout(button_layout)
        self.setLayout(layout)
        self.adjustSize()

        if (
            self.data["expire_timeout"]
            and self.data.get("hints", {}).get("urgency") != 2
        ):
            self.displayed.connect(self.schedule_close)

    def schedule_close(self):
        timeout = (
            5000  # TODO (low) make this a config
            if self.data["expire_timeout"] == -1
            else self.data["expire_timeout"]
        )
        logger.info(
            f"Scheduling close of notification {self.data['id']} in {timeout / 1000} seconds"
        )
        QTimer.singleShot(
            timeout,
            lambda: self.closed.emit(NotificationCloseReason.EXPIRED.value),
        )
