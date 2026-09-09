#!/usr/bin/env python3
# check_corpus_reproducibility.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Check every delivered corpus, or emit validated read-only checkout inputs."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

from corpus_registry import ROOT, validate_registration, verify_checkout


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--github-output", type=Path, help="append validated checkout outputs to this file")
    modes.add_argument("--foundry", type=Path, help="clean checkout of the registered Foundry revision")
    args = parser.parse_args(argv)
    try:
        registry = validate_registration(ROOT)
        if args.github_output is not None:
            paths = sorted(record["source"] for record in registry["corpora"].values())
            # Only validated path characters reach the protocol. This delimiter
            # cannot equal a normalized source path containing '/' characters.
            output = (f"repository={registry['repository']}\nrevision={registry['revision']}\n"
                      + "sparse_paths<<CORPUS_PATHS_END\n" + "\n".join(paths) + "\nCORPUS_PATHS_END\n")
            with args.github_output.open("a", encoding="utf-8") as stream:
                stream.write(output)
            return 0
        foundry = args.foundry.resolve()
        verify_checkout(foundry, registry, registry["revision"])
        for name, record in sorted(registry["corpora"].items()):
            if record["state"] == "pending":
                print(f"pending corpus {name}: no imported tree; reproducibility not claimed", flush=True)
                continue
            importer = ROOT / record["importer"]
            completed = subprocess.run([sys.executable, str(importer), "--foundry", str(foundry),
                                        "--revision", registry["revision"], "--check"], check=False)
            if completed.returncode:
                print(f"corpus {name}: importer {record['importer']} failed (exit {completed.returncode})", file=sys.stderr)
                return completed.returncode if completed.returncode > 0 else 1
            print(f"checked corpus {name}: {record['destination']} matches {registry['revision']}", flush=True)
        return 0
    except (ValueError, OSError) as error:
        print(f"corpus reproducibility: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
