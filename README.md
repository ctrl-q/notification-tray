# notification-tray

FreeDesktop-compliant notification daemon, with support for:
- Notification grouping
- Do not Disturb
- Notification batching
- Custom notification sounds

## Getting started

1. Run `pip install git+https://github.com/ctrl-q/notification-tray.git`

## Customization

### Notification grouping

notification-tray uses the directory structure of dbus-to-json to group notifications.

To change how notifications are grouped, set `subdir_callback` as described in dbus-to-json

### Do not Disturb

notification-tray provides a do not disturb submenu under every folder, with 3 preset values: 1 hour, 8 hours and forever

To set a different value, set `do_not_disturb_until` to an iso-formatted future date in .settings.json

### Notification batching

If you receives multiple notifications in quick succession, and would like to receive them in one batch instead, set `notification_backoff_minutes` in .settings.json

### Custom notification sounds

Place a .notification.wav file under any folder to set it as the notification sound for that folder
