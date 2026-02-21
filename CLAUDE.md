# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build (from project root)
mkdir -p build && cd build && cmake .. && make -j$(nproc)

# Build with tests enabled
cmake -DENABLE_TESTING=ON .. && make -j$(nproc)

# Run all tests
./notification-tray-tests

# Run specific test suite
./notification-tray-tests --gtest_filter="SettingsTest.*"

# Run CI checks locally (build, test, format-check, cppcheck)
make ci

# Format code
make format

# Check formatting without modifying
make format-check
```

## Testing

Tests use Google Test/Google Mock. Test files are in `tests/` and follow the pattern `test_<component>.cpp`. Tests require `QT_QPA_PLATFORM=offscreen` when running headless.

CI enforces 85% code coverage threshold.

## Architecture

This is a FreeDesktop-compliant notification daemon built with Qt5 and D-Bus.

**Core data flow:**
1. **NotificationService** receives D-Bus notifications via the FreeDesktop Notifications spec
2. **Notifier** manages display logic, Do Not Disturb filtering, and notification backoff/batching
3. **NotificationCacher** persists notifications to JSON files in a hierarchical directory structure
4. **Tray** provides system tray UI with a context menu built from the notification folder hierarchy

**Key types (`include/notification_types.h`):**
- `Notification` - incoming notification data from D-Bus
- `CachedNotification` - persisted notification with path and closed state
- `NotificationFolder` - recursive tree structure for organizing notifications

**Notification storage:** Notifications are stored at `slugify(app_name)/slugify(summary)/<run_id>-<id>.json`. Per-folder `.settings.json` files can customize behavior (grouping via `subdir_callback`, Do Not Disturb, backoff timing, sounds).

**Python integration:** The `subdir_callback` setting evaluates Python lambda expressions to customize notification grouping paths.
