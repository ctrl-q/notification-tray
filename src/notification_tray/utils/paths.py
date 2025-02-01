import json
import logging
import re
import sys
import time
import unicodedata
from pathlib import Path

from notification_tray.types.notification import Notification
from notification_tray.utils.logging import log_input_and_output


@log_input_and_output(logging.DEBUG)
def get_output_path(root_path: Path, notification: Notification) -> Path:
    def slugify(s: str):
        """Adapted from https://docs.djangoproject.com/en/4.1/ref/utils/#django.utils.text.slugify"""
        s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")
        s = re.sub(r"[^\w\s-]", "", s.lower())
        return re.sub(r"[-\s]+", "-", s).strip("-_")

    def get_outdir() -> Path:
        default_outdir = (
            root_path
            / slugify(notification["app_name"])
            / slugify(notification["summary"])
        )
        try:
            for folder in reversed(
                [default_outdir, *default_outdir.relative_to(root_path).parents]
            ):
                folder = root_path / folder
                if (
                    settings_file := root_path / folder / ".settings.json"
                ).exists() and (
                    subdir_callback := json.loads(settings_file.read_text()).get(
                        "subdir_callback"
                    )
                ):
                    subdir_path = eval(subdir_callback)(notification.copy())
                    match subdir_path:
                        case [] | None:
                            pass
                        case [*strings] if all(isinstance(s, str) for s in strings):
                            if subdir_path:
                                if (
                                    folder.resolve()
                                    in (
                                        outdir := (
                                            folder.joinpath(
                                                *map(slugify, map(str, strings))
                                            )
                                        ).resolve()
                                    ).parents
                                ):
                                    return outdir
                                else:
                                    print(
                                        f"Error: Subdir must be below {folder}, got {outdir}"
                                    )
                                    return default_outdir
                        case _:
                            print(
                                f"Error: Expected list of strings from subdir_callback. Got {type(subdir_path)}"
                            )
                            return default_outdir
        except Exception as e:
            print("Error:", e, file=sys.stderr)
        return default_outdir

    return get_outdir() / f"{time.strftime('%Y%m%d-%H%M%S')}-{notification['id']}.json"
