#!/usr/bin/env python3
# check_format.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Check maintained tracked C/C++ files with the pinned formatter; --fix explicitly repairs them."""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

from build_config import load_config, require_equal
from add_license_header import C_STYLE_SUFFIXES, REPOSITORY_ROOT, is_excluded

PIN_PATH = REPOSITORY_ROOT / "scripts" / "requirements-format.txt"
INSTALL = "python3 -m pip install -r scripts/requirements-format.txt"


def maintained_files() -> list[Path]:
    """Use the license policy and Git's index, including newly staged native tests."""
    result = subprocess.run(["git", "ls-files", "-z"], cwd=REPOSITORY_ROOT,
                            capture_output=True, check=True)
    paths = [REPOSITORY_ROOT / os.fsdecode(name) for name in result.stdout.split(b"\0") if name]
    selected = [path for path in paths if path.suffix in C_STYLE_SUFFIXES and not is_excluded(path)]
    if not selected:
        raise ValueError("No maintained tracked C/C++ files discovered; check the checkout and Git index")
    for path in selected:
        if not path.is_file():
            raise ValueError(f"Missing maintained tracked source: {path.relative_to(REPOSITORY_ROOT)}")
    return selected


def require_formatter(executable: str) -> None:
    pin = re.fullmatch(r"clang-format==(\d+\.\d+\.\d+)\s*", PIN_PATH.read_text())
    if pin is None:
        raise ValueError(f"Invalid exact clang-format pin in {PIN_PATH}")
    version = load_config()["toolchain"]["clang_format"]
    require_equal("generated formatter requirements projection", version, pin.group(1))
    try:
        result = subprocess.run([executable, "--version"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(f"Cannot run formatter {executable!r}: {error}. Install with: {INSTALL}") from error
    output = (result.stdout + result.stderr).strip()
    actual = re.search(r"\bclang-format version (\d+\.\d+\.\d+)(?:\s|$)", output)
    if actual is None or actual.group(1) != version:
        raise ValueError(f"Formatting requires clang-format {version}; {executable!r} reported {output!r}. "
                         f"Install with: {INSTALL}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true", help="explicitly format the same maintained tracked files")
    parser.add_argument("--clang-format", default="clang-format", help="formatter executable (must match the pin)")
    arguments = parser.parse_args()
    try:
        paths = maintained_files()
        require_formatter(arguments.clang_format)
        mode = ["-i"] if arguments.fix else ["--dry-run", "--Werror"]
        result = subprocess.run([arguments.clang_format, f"--style=file:{REPOSITORY_ROOT / '.clang-format'}",
                                 *mode, *[str(path) for path in paths]], cwd=REPOSITORY_ROOT)
        if result.returncode:
            return 1
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Format check failed: {error}", file=sys.stderr)
        return 1
    print(f"{'Formatted' if arguments.fix else 'Checked'} {len(paths)} maintained C/C++ file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
