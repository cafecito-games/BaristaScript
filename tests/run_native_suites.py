#!/usr/bin/env python3
# run_native_suites.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Supervise native C++ suites hosted by an explicitly selected, unmodified Godot."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from native_test_build import build_identity
from build_config import load_config, parse_json, require_equal
from build_metadata import create_metadata, inspect_artifact, verify_identity

DEFAULT_BUILD_DIR = ROOT / "build/native-scons"
PROTOCOL = (ROOT / "tests/native/result_protocol.h").read_text()
RESULT_PREFIX = re.search(r'#define BS_NATIVE_RESULT_PREFIX "([^"]+)"', PROTOCOL)[1]
PROTOCOL_VERSION = int(re.search(r"#define BS_NATIVE_PROTOCOL_VERSION (\d+)", PROTOCOL)[1])
RESULT_FORMAT = json.loads(next(line.split(" ", 2)[2] for line in PROTOCOL.splitlines()
                                if line.startswith("#define BS_NATIVE_RESULT_FORMAT ")))
RESULT_FIELDS = set(re.findall(r'"([a-z_]+)":', RESULT_FORMAT))


def select_suites(requested):
    suites = json.loads((ROOT / "tests/native_suites.json").read_text())["suites"]
    if not suites or len(set(suites)) != len(suites) or any(not re.fullmatch(r"[a-z0-9_]+", s) for s in suites):
        raise ValueError("native suite manifest must contain unique, nonempty suite names")
    if set(requested) - set(suites):
        raise ValueError(f"unknown suite: {sorted(set(requested) - set(suites))}")
    return requested or suites


def editor_import_suites():
    manifest = json.loads((ROOT / "tests/native_suites.json").read_text())
    imports = manifest.get("editor_import", [])
    if (not isinstance(imports, list) or any(not isinstance(suite, str) for suite in imports)
            or len(set(imports)) != len(imports) or not set(imports) <= set(manifest["suites"])):
        raise ValueError("native editor_import must name unique required suites")
    return imports


def prepare_project(godot, project, suite, timeout):
    if suite not in editor_import_suites():
        return
    completed = supervise([str(godot), "--headless", "--path", str(project), "--editor", "--quit"], timeout)
    if completed.returncode or not (project / ".godot/global_script_class_cache.cfg").is_file():
        raise ValueError(f"native {suite} editor preparation failed: {completed.stdout}")


def read_artifact(build_dir):
    artifact = json.loads((build_dir / "native-artifact.json").read_text())
    library = Path(artifact["library"])
    if not library.is_file() or hashlib.sha256(library.read_bytes()).hexdigest() != artifact["sha256"]:
        raise ValueError(f"missing or wrong native test library: {library}; rebuild with tests enabled")
    if artifact.get("build_id") != build_identity():
        raise ValueError("native test artifact does not match current build inputs; rebuild with tests enabled")
    actual = inspect_artifact(library)
    verify_identity(actual, artifact["build_info"])
    config = load_config()
    build = actual["build"]
    expected = create_metadata(ROOT, config, dict(platform=build["platform"], architecture=build["architecture"],
                               target=build["target"], precision=config["precision"], api=config["godot_api"]), native_tests=True)
    verify_identity(actual, expected)
    # The native source fingerprint covers mutable extension/test inputs, not arbitrary
    # dependency edits: require a clean selected dependency for a current-artifact claim.
    require_equal("native godot-cpp state", "clean", actual["godot_cpp"]["state"])
    require_equal("native selected godot-cpp revision", actual["godot_cpp"]["selected_revision"], actual["godot_cpp"]["revision"])
    return artifact


def godot_data_path():
    """Desktop host data roots used by stock Godot's custom user directory setting."""
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support"
    if sys.platform == "win32":
        return Path(os.environ["APPDATA"])
    if sys.platform.startswith("linux"):
        return Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
    raise ValueError(f"unsupported native test host: {sys.platform}")


@contextmanager
def staged_project(artifact):
    # No import runs against the ordinary fixture. Its res:// paths remain identical, while
    # writable state, descriptor and the selected library belong to this disposable project.
    data_path = godot_data_path()
    data_path.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="barista-native-", dir=data_path) as temporary:
        project = Path(temporary) / "project"
        shutil.copytree(ROOT / "project", project,
                        ignore=shutil.ignore_patterns(".godot", "bin"))
        # Stock Godot checks for a main scene before instantiating a native --main-loop.
        # This empty scene supplies project selection only; it contains no test bootstrap.
        descriptor = project / "project.godot"
        descriptor.write_text(descriptor.read_text().replace(
            "[application]", '[application]\nrun/main_scene="res://native_host.tscn"\n'
            'config/use_custom_user_dir=true\n'
            f'config/custom_user_dir_name="{Path(temporary).name}"', 1))
        (project / "native_host.tscn").write_text('[gd_scene format=3]\n\n[node name="NativeHost" type="Node"]\n')
        (project / "bin").mkdir()
        library = Path(artifact["library"])
        destination = project / "bin" / ("native" + library.suffix)
        shutil.copy2(library, destination)
        (project / "bin/barista_script.gdextension").write_text(
            '[configuration]\nentry_symbol = "barista_script_library_init"\n'
            f'compatibility_minimum = "{load_config()["godot_api"]}"\nreloadable = false\n\n[libraries]\n'
            f'debug = "res://bin/{destination.name}"\nrelease = "res://bin/{destination.name}"\n')
        (project / ".godot").mkdir()
        shutil.copy2(ROOT / "project/.godot/extension_list.cfg", project / ".godot/extension_list.cfg")
        yield project


def supervise(command, timeout):
    try:
        return subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or b""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        return subprocess.CompletedProcess(command, 124, output + f"\ntimeout after {timeout}s\n")
    except OSError as error:
        return subprocess.CompletedProcess(command, 127, str(error))


def invoke(godot, project, suite, case, nonce, timeout, listing=False):
    command = [str(godot), "--headless", "--path", str(project), "--main-loop", "BaristaNativeTestRunner",
               "--", f"--native-suite={suite}", f"--native-case={case}", f"--native-nonce={nonce}"]
    if listing:
        command.append("--native-list")
    return supervise(command, timeout)


def evaluate(returncode, output, suite, case, nonce, build_id, build_info):
    failures = []
    if returncode:
        failures.append(f"process exited {returncode}")
    records = [line[len(RESULT_PREFIX):] for line in output.splitlines() if line.startswith(RESULT_PREFIX)]
    if len(records) != 1:
        return failures + [f"expected one completion record, found {len(records)}"]
    try:
        record = parse_json(records[0])
        if not isinstance(record, dict) or set(record) != RESULT_FIELDS:
            raise ValueError("completion fields do not match the shared protocol")
        for key, expected in dict(protocol=PROTOCOL_VERSION, suite=suite, case=case, nonce=nonce,
                                  build_id=build_id).items():
            if type(record[key]) is not type(expected) or record[key] != expected:
                failures.append(f"completion {key} does not match {expected!r}")
        verify_identity(record["build_info"], build_info)
        for key in ("cases", "assertions", "failed_cases", "failed_assertions"):
            if type(record[key]) is not int or record[key] < 0:
                raise ValueError(f"invalid {key} count")
        if record["cases"] == 0 or record["assertions"] == 0:
            failures.append("zero executed cases or assertions")
        if record["failed_cases"] or record["failed_assertions"]:
            failures.append("native assertions failed")
    except (ValueError, KeyError, TypeError) as error:
        failures.append(f"invalid completion record: {error}")
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--godot", required=True, help="explicit Godot executable")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR)
    parser.add_argument("--suite", action="append", default=[])
    parser.add_argument("--case", default="", help="exact registered case name (no wildcards)")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--list", action="store_true", help="informational case listing; never verifies a suite")
    args = parser.parse_args(argv)
    try:
        suites = select_suites(args.suite)
        artifact = read_artifact(args.build_dir)
        if not math.isfinite(args.timeout) or args.timeout <= 0 or any(c in args.case for c in "*?,"):
            raise ValueError("timeout must be positive and --case must be an exact name")
        failed = False
        for suite in suites:
            nonce = uuid.uuid4().hex
            with staged_project(artifact) as project:
                prepare_project(args.godot, project, suite, args.timeout)
                completed = invoke(args.godot, project, suite, args.case, nonce, args.timeout, args.list)
            print(completed.stdout, end="", flush=True)
            reasons = ([f"listing process exited {completed.returncode}"] if completed.returncode else []) if args.list else evaluate(
                completed.returncode, completed.stdout, suite, args.case, nonce, artifact["build_id"], artifact["build_info"])
            for reason in reasons:
                print(f"FAIL {suite}: {reason}", file=sys.stderr)
            failed |= bool(reasons)
            if not reasons and not args.list:
                print(f"PASS native {suite}")
        return int(failed)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"FAIL native runner: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
