# notification-tray

FreeDesktop-compliant notification daemon, with support for:
- Notification grouping
- Do not Disturb
- Notification batching
- Custom notification sounds
- Notification persistence

## Getting started

1. Clone this repository
1. Run `pip install -r requirements.txt`
1. Run `python __init__.py <path to notification storage directory>`


## Customization

There are two types of customization options: global and local.

### Global options

#### Theming

notification-tray reads your theming config from your LXQt configuration file, either `.config/lxqt/lxqt.conf` or `/usr/share/lxqt/lxqt.conf`.

> [!NOTE]

Even if you don't use LXQt, you can still set those settings.

##### Icon theme

To set your [icon theme](https://specifications.freedesktop.org/icon-theme-spec), set the following config in your configuration file:

```ini
[General]
icon_theme=<your_icon_theme_name>
```

##### Notification theme

notification-tray is compatible with [LXQt-notificationd themes](https://github.com/lxqt/lxqt/wiki/Creating-LXQt-Themes#the-lxqt-notification-daemon-pathlxqt-notificationd).

To set your notification theme, set the following config in your configuration file:

```ini
[General]
theme=<your_theme_name>
```

### Local options

Local options are specific to each notification and are configurable using a different files.

The files can be placed anywhere in between the base notification storage directory and the final directory of a notification. Children settings take precedence over their parent settings

#### Notification grouping

By default, every notification is stored under the notification storage directory, at `slugify(<app_name>)/slugify(<summary>)/<unique run ID>-<id>.json`

To change this, set `subdir_callback` in a .settings.json in any folder in that directory tree

```json5
{
    // (optional) lambda expression taking a notification dict as input, and returning either a list of strings representing the relative path, or None
    // If the return value is None or contains no non-empty strings, the default outdir is used instead
    "subdir_callback": "lambda notification: 'some subdirectory'"
}
```

#### Do not Disturb

notification-tray provides a do not disturb submenu under every folder, with 3 preset values: 1 hour, 8 hours and forever

To set a different value, set `do_not_disturb_until` to an iso-formatted future date in .settings.json

#### Notification batching

If you receive multiple notifications in quick succession, and would like to receive them in one batch instead, set `notification_backoff_minutes` to a integer in .settings.json

#### Custom notification sounds

Place a .notification.wav file under any folder to set it as the notification sound for that folder

#### Notification timeout

To change the default notification timeout, set the environment variable `NOTIFICATION_TRAY_DEFAULT_TIMEOUT_MILLIS` to a nonzero positive integer
