#!/usr/bin/env python3

import argparse
import json
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path


RECOGNIZED_FIELDS = {
    "subdir_callback",
    "do_not_disturb_until",
    "hide_from_tray_until",
    "notification_backoff_minutes",
    "sound",
}


def default_input_root() -> Path:
    return Path.home() / ".local" / "share" / "dbus-to-json"


def default_output_path() -> Path:
    return Path.home() / ".config" / "notification-tray" / "settings.json"


def folder_key(input_root: Path, settings_file: Path) -> str:
    parent = settings_file.parent
    relative = parent.relative_to(input_root)
    if str(relative) in ("", "."):
        return "."
    return relative.as_posix()


def load_existing_output(path: Path) -> dict:
    if not path.exists():
        return {"version": 1, "folders": {}}
    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
        if not isinstance(data, dict):
            return {"version": 1, "folders": {}}
        data.setdefault("version", 1)
        if not isinstance(data.get("folders"), dict):
            data["folders"] = {}
        return data
    except Exception:
        return {"version": 1, "folders": {}}


def backup_output(path: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = path.with_name(f"{path.name}.bak.{stamp}")
    shutil.copy2(path, backup)
    return backup


def migrate(input_root: Path, output_path: Path, dry_run: bool) -> int:
    if not input_root.exists():
        print(f"Input root does not exist: {input_root}", file=sys.stderr)
        return 1

    settings_files = sorted(input_root.rglob(".settings.json"))
    config = load_existing_output(output_path)
    folders = config["folders"]

    migrated_files = 0
    invalid_files = 0

    for file_path in settings_files:
        try:
            with file_path.open("r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as exc:
            invalid_files += 1
            print(f"Invalid JSON in {file_path}: {exc}", file=sys.stderr)
            continue

        if not isinstance(data, dict):
            invalid_files += 1
            print(f"Invalid object in {file_path}: top-level JSON is not an object", file=sys.stderr)
            continue

        key = folder_key(input_root, file_path)
        section = folders.get(key, {})
        if not isinstance(section, dict):
            section = {}

        changed = False
        for field in RECOGNIZED_FIELDS:
            if field in data:
                section[field] = data[field]
                changed = True

        if changed:
            folders[key] = section
            migrated_files += 1

    config["version"] = 1
    config["folders"] = dict(sorted(folders.items(), key=lambda kv: kv[0]))

    if dry_run:
        print(f"Discovered {len(settings_files)} .settings.json files")
        print(f"Migrated {migrated_files} files")
        if invalid_files:
            print(f"Skipped {invalid_files} invalid files", file=sys.stderr)
        print(json.dumps(config, indent=2, sort_keys=False))
        return 0

    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        backup = backup_output(output_path)
        print(f"Backed up existing config to: {backup}")

    with output_path.open("w", encoding="utf-8") as f:
        json.dump(config, f, indent=2)
        f.write("\n")

    print(f"Wrote migrated config to: {output_path}")
    print(f"Discovered {len(settings_files)} .settings.json files")
    print(f"Migrated {migrated_files} files")
    if invalid_files:
        print(f"Skipped {invalid_files} invalid files", file=sys.stderr)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Migrate per-folder .settings.json files into global notification-tray settings.json"
    )
    parser.add_argument(
        "--input-root",
        type=Path,
        default=default_input_root(),
        help="Notification storage root (default: ~/.local/share/dbus-to-json)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=default_output_path(),
        help="Output settings file (default: ~/.config/notification-tray/settings.json)",
    )
    parser.add_argument("--dry-run", action="store_true", help="Print migrated config without writing")
    args = parser.parse_args()

    return migrate(args.input_root.expanduser(), args.output.expanduser(), args.dry_run)


if __name__ == "__main__":
    raise SystemExit(main())
