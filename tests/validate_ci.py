#!/usr/bin/env python3
# validate_ci.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    api_path = ROOT / "godot-cpp" / "gdextension" / "extension_api-4-7.json"
    workflow_path = ROOT / ".github" / "workflows" / "ci.yml"

    api_precision = json.loads(api_path.read_text())["header"]["precision"]
    workflow = workflow_path.read_text()
    matrix_precisions = re.findall(r"^\s+float-precision:\s+(\w+)\s*$", workflow, re.MULTILINE)

    if not matrix_precisions:
        print("No CI float-precision matrix entries found")
        return 1

    incompatible = sorted({precision for precision in matrix_precisions if precision != api_precision})
    if incompatible:
        print(
            f"CI requests {', '.join(incompatible)} precision, but the Godot 4.7 API is {api_precision} precision"
        )
        return 1

    if "  push:\n    branches: [main]\n" not in workflow:
        print("CI push events must be limited to main to avoid duplicating pull request runs")
        return 1

    # A GDScript parse error makes SceneTree quit 0, so a suite invoked straight from the
    # workflow can stop testing while the job stays green. Every suite must go through
    # tests/run_gdscript_suites.py, which demands the guard sentinel.
    direct_invocations = re.findall(r"--script\s+res://\S+", workflow)
    if direct_invocations:
        print(
            "CI must not invoke a GDScript suite directly "
            f"({', '.join(sorted(set(direct_invocations)))}); "
            "run it through tests/run_gdscript_suites.py so a parse error cannot pass as green"
        )
        return 1

    if "tests/run_gdscript_suites.py" not in workflow:
        print("CI must run the GDScript suites through tests/run_gdscript_suites.py")
        return 1

    print(f"CI configuration matches the Godot 4.7 API ({api_precision}) and avoids duplicate PR runs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
