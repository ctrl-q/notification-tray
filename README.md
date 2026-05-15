# notification-tray

FreeDesktop-compliant notification daemon, with support for:
- Notification grouping
- Do not Disturb
- Notification batching
- Custom notification sounds
- Notification persistence

## Getting started

### Building from source

1. Clone this repository
2. Install dependencies:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install build-essential cmake qt5-default libqt5multimedia5-dev libdbus-1-dev libglib2.0-dev python3-dev

   # Fedora
   sudo dnf install gcc-c++ cmake qt5-qtbase-devel qt5-qtmultimedia-devel dbus-devel glib2-devel python3-devel

   # Arch Linux
   sudo pacman -S base-devel cmake qt5-base qt5-multimedia dbus glib2 python
   ```

3. Build:
   ```bash
   mkdir -p build && cd build
   cmake ..
   make
   ```

4. Run:
   ```bash
   ./notification-tray <path to notification storage directory>
   ```

### Installation

#### Arch Linux

```bash
yay -S notificaion-tray-git
```

#### From source

```bash
sudo make install
```

### Running as a systemd service

After installation, you can run notification-tray as a systemd user service:

```bash
# Enable and start the service
systemctl --user enable --now notification-tray.service

# Check status
systemctl --user status notification-tray

# View logs
journalctl --user -u notification-tray -f
```

The service will automatically store notifications in `~/.local/share/notification-tray`.

To stop or disable the service:

```bash
# Stop the service
systemctl --user stop notification-tray

# Disable autostart
systemctl --user disable notification-tray
```


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

Local options are configured in a single global JSON file:

`~/.config/notification-tray/settings.json`

The path follows Qt's `QStandardPaths::ConfigLocation` and may vary if your environment overrides XDG config paths.

Configuration uses a `folders` map keyed by notification folder path relative to the storage root:

- `"."` for the root
- `"app"` for an app folder
- `"app/summary"` for nested folders

Example:

```json
{
  "version": 1,
  "folders": {
    ".": {
      "notification_backoff_minutes": 5
    },
    "firefox": {
      "sound": "notify.wav"
    },
    "firefox/new-tab": {
      "do_not_disturb_until": "2026-05-15T22:00:00Z",
      "hide_from_tray_until": "2026-05-15T23:00:00Z"
    }
  }
}
```

For `notification_backoff_minutes`, `do_not_disturb_until`, `hide_from_tray_until`, and `sound`, children override parents (closest ancestor wins).

#### Notification grouping

By default, every notification is stored under the notification storage directory, at `slugify(<app_name>)/slugify(<summary>)/<unique run ID>-<id>.json`

To change this, set `subdir_callback` in the corresponding folder entry under `folders`.

```json5
{
    // (optional) JavaScript arrow function taking a notification object as input, and returning either an array of strings representing the relative path, or null
    // If the return value is null/undefined or contains no non-empty strings, the default outdir is used instead
    "subdir_callback": "(n) => ['some subdirectory']"
}
```

#### Do Not Disturb

notification-tray provides a do not disturb submenu under every folder, with 3 preset values: 1 hour, 8 hours and forever

To set a different value, set `do_not_disturb_until` to an ISO-formatted future date in that folder section.

#### Notification batching

If you receive multiple notifications in quick succession, and would like to receive them in one batch instead, set `notification_backoff_minutes` to an integer in that folder section.

#### Custom notification sounds

Set `sound` to a file path to play a custom sound for notifications in that folder section. The path can be absolute or relative to the folder key where the setting is defined. Child folders inherit the setting from parent folders, and a more specific setting takes precedence.

```json
{ "sound": "notify.wav" }
```

#### Migration from legacy `.settings.json`

Use `scripts/migrate_settings.py` to convert existing legacy files under `~/.local/share/dbus-to-json` into the global config:

```bash
python3 scripts/migrate_settings.py --dry-run
python3 scripts/migrate_settings.py
```

Useful options:

- `--input-root <path>` to migrate from a custom storage root
- `--output <path>` to write to a custom settings file
- Existing output files are backed up automatically before overwrite

#### Notification timeout

To change the default notification timeout, set the environment variable `NOTIFICATION_TRAY_DEFAULT_TIMEOUT_MILLIS` to a nonzero positive integer

#### Log formatting

The log output format can be customized using the following environment variables:

| Variable | Description | Default |
|----------|-------------|---------|
| `NOTIFICATION_TRAY_LOG_FORMAT` | Format string with placeholders | `{syslog_prefix}{timestamp} [{level}] {name}: {message}` |
| `NOTIFICATION_TRAY_LOG_TIMESTAMP_FORMAT` | Qt datetime format string | `yyyy-MM-ddTHH:mm:ss` |

**Placeholders for `NOTIFICATION_TRAY_LOG_FORMAT`:**
- `{timestamp}` - formatted timestamp
- `{level}` - log level (DEBUG, INFO, WARNING, ERROR)
- `{name}` - logger name (e.g., "Tray", "Notifier")
- `{message}` - the log message
- `{syslog_prefix}` - syslog numeric prefix like `<6>`

**Examples:**

```bash
# Simple format without syslog prefix
export NOTIFICATION_TRAY_LOG_FORMAT="{timestamp} [{level}] {message}"

# Time only with milliseconds
export NOTIFICATION_TRAY_LOG_TIMESTAMP_FORMAT="HH:mm:ss.zzz"

# Minimal format
export NOTIFICATION_TRAY_LOG_FORMAT="[{level}] {message}"
```

## Development

This is a C++ application using Qt5 for the GUI and D-Bus for the notification protocol.

### Project Structure

```
notification-tray-cpp/
├── include/                    # Header files
│   ├── notification_types.h    # Core data structures
│   ├── notification_service.h  # D-Bus service interface
│   ├── notification_cacher.h   # Notification storage
│   ├── notifier.h              # Notification display logic
│   ├── tray.h                  # System tray integration
│   ├── notification_widget.h   # Notification popup widget
│   ├── notification_timer.h    # Pauseable timer
│   └── utils/                  # Utility classes
│       ├── logging.h           # Logging system
│       ├── settings.h          # Settings management
│       └── paths.h             # Path utilities
├── src/                        # Implementation files
│   ├── main.cpp                # Entry point
│   └── *.cpp                   # Component implementations
├── tests/                      # Unit tests (Google Test)
│   ├── test_main.cpp           # Test runner
│   └── test_*.cpp              # Test files
├── CMakeLists.txt              # Build configuration
├── .clang-format               # Code formatting rules
├── .clang-tidy                 # Static analysis rules
├── .editorconfig               # Editor settings
├── .pre-commit-config.yaml     # Pre-commit hooks
```

### Dependencies

- Qt5 (Core, Widgets, Multimedia, DBus, Test, Qml)
- D-Bus 1.0
- GLib 2.0
- C++20 compiler (GCC 10+, Clang 10+)
- Google Test (for unit tests)

### Building

```bash
# Standard build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Build with tests
cmake -DENABLE_TESTING=ON ..
make -j$(nproc)

# Build with code coverage
cmake -DENABLE_COVERAGE=ON ..
make -j$(nproc)

# Build with sanitizers (ASan + UBSan)
cmake -DENABLE_SANITIZERS=ON ..
make -j$(nproc)
```

### Testing

```bash
# Run all tests
./notification-tray-tests

# Run specific test suite
./notification-tray-tests --gtest_filter="SettingsTest.*"

# Run with verbose output
./notification-tray-tests --gtest_print_time=1

# List all tests
./notification-tray-tests --gtest_list_tests
```

### Code Coverage

```bash
# Build with coverage enabled
cmake -DENABLE_COVERAGE=ON ..
make

# Generate coverage report
make coverage

# View report
xdg-open coverage_report/index.html
```

### Code Quality Tools

```bash
# Run everything needed to pass CI (build, test, format-check, cppcheck)
make ci

# Format all source files
make format

# Check formatting (CI-friendly, doesn't modify files)
make format-check

# Run clang-tidy static analysis
make tidy

# Run cppcheck
make cppcheck

# Run all analysis tools
make analyze
```

### Pre-commit Hooks

Pre-commit hooks are configured to run formatting and linting automatically:

```bash
# Install pre-commit (if not already installed)
pip install pre-commit

# Install hooks
pre-commit install

# Run hooks manually on all files
pre-commit run --all-files
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_TESTING` | `ON` | Enable unit tests |
| `ENABLE_COVERAGE` | `OFF` | Enable code coverage instrumentation |
| `ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer |

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   SystemTrayFileBrowser                      │
│                    (Main Application)                        │
└─────────────┬───────────────┬───────────────┬───────────────┘
              │               │               │
              ▼               ▼               ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────┐
│ NotificationSvc │ │    Notifier     │ │      Tray       │
│   (D-Bus API)   │ │ (Display Logic) │ │ (System Tray)   │
└────────┬────────┘ └────────┬────────┘ └────────┬────────┘
         │                   │                   │
         ▼                   ▼                   │
┌─────────────────┐ ┌─────────────────┐          │
│ NotificationCacher │ NotificationWidget│        │
│  (Persistence)  │ │   (Popup UI)    │          │
└─────────────────┘ └─────────────────┘          │
                                                 │
                    ┌────────────────────────────┘
                    ▼
           ┌─────────────────┐
           │     Utils       │
           │ (Settings,Paths,│
           │    Logging)     │
           └─────────────────┘
```

**Key Components:**

- **SystemTrayFileBrowser**: Main application class that orchestrates all components
- **NotificationService**: D-Bus interface implementing FreeDesktop Notifications spec
- **Notifier**: Handles notification display logic, filtering (DnD, backoff), and widget management
- **NotificationCacher**: Manages notification persistence to filesystem
- **Tray**: System tray icon and context menu
- **NotificationWidget**: Individual notification popup window
