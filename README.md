# notification-tray

Notification tray built on top of [dbus-to-json](https://github.com/ctrl-q/dbus-to-json), with support for:
- Notification grouping
- Do not Disturb
- Notification batching
- Custom notification sounds

Runs alongside your existing notification tray (see TODO)

## Getting started

1. Run dbus-to-json
1. Install [uv](https://docs.astral.sh/uv)
1. Run `uv run main.py <dbus-to-json outdir>`

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

## How it works

notification-tray listens to signals emitted by dbus-to-json, and does the following:
1. If notification batching or do not disturb is enabled, close the notification
1. If notification batching is enabled, wait until the specified backoff to display the notification in the next batch

## TODO

- [ ] Support actions in the notification that notification-tray provides.

This lack of support is the only reason existing notification trays cannot be replaced and is the reason why notification-tray uses `replaces_id` (because, at least in LXQt, this preserves the actions from the original notification that LXQt presented)