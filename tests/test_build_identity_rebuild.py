#!/usr/bin/env python3
# test_build_identity_rebuild.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Run real SCons/CMake identity regressions in disposable clones, never the caller's tree.

This opt-in integration check requires a C++ toolchain, the selected SCons version,
CMake, stock Godot, and an initialized local godot-cpp checkout. Logs and retained
artifacts are written outside every changing checkout and survive failures.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
SUFFIXES = {"darwin": ".dylib", "linux": ".so", "win32": ".dll"}
PLATFORMS = {"darwin": "macos", "linux": "linux", "win32": "windows"}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def config_sha(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True).encode()).hexdigest()


class Regression:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=False)
        self.report = {"source": str(args.source), "revision": args.revision, "commands": [], "states": [], "status": "running", "host": {"platform": platform.platform(), "architecture": args.architecture, "python": sys.version}}
        self.env = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_OPTIONAL_LOCKS="0")
        # A user's Git environment must not redirect operations outside our clones.
        for key in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
            self.env.pop(key, None)
        shutil.copyfile(__file__, self.output / "driver.py")
        self.report["driver_sha256"] = sha(self.output / "driver.py")
        self.save()

    def save(self):
        path = self.output / "report.json"
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.report, indent=2) + "\n")
        temporary.replace(path)

    def command(self, label, command, cwd=None, *, failure=None):
        command = [str(value) for value in command]
        index = len(self.report["commands"])
        log = self.output / f"{index:03d}-{label}.log"
        record = {"label": label, "command": command, "cwd": str(cwd or self.output), "log": str(log)}
        self.report["commands"].append(record)
        self.save()
        started = time.monotonic()
        with log.open("w") as stream:
            completed = subprocess.run(command, cwd=cwd or self.output, env=self.env, stdout=stream, stderr=subprocess.STDOUT)
        output = log.read_text(errors="replace")
        record.update(returncode=completed.returncode, seconds=round(time.monotonic() - started, 3), log_sha256=sha(log))
        self.save()
        if failure is None:
            require(completed.returncode == 0, f"{label} exited {completed.returncode}; see {log}")
        else:
            require(completed.returncode != 0, f"{label} unexpectedly accepted invalid identity; see {log}")
            require(failure in output, f"{label} failed for an unrelated reason; expected {failure!r}; see {log}")
        return output

    def git(self, root, *arguments):
        return self.command("git-" + arguments[0], ["git", "-C", root, *arguments]).strip()

    def clone(self, system):
        root = self.output / system
        self.command("clone-" + system, ["git", "clone", "--no-hardlinks", "--no-checkout", self.args.source, root])
        self.git(root, "checkout", "--detach", self.args.revision)
        dependency = self.git(root, "ls-tree", "HEAD", "godot-cpp").split()[2]
        self.command("clone-dependency-" + system, ["git", "clone", "--no-hardlinks", "--no-checkout", self.args.source / "godot-cpp", root / "godot-cpp"])
        self.git(root / "godot-cpp", "checkout", "--detach", dependency)
        self.git(root, "config", "user.name", "Build identity regression")
        self.git(root, "config", "user.email", "build-identity@example.invalid")
        hooks = self.output / "empty-hooks"
        hooks.mkdir(exist_ok=True)
        self.git(root, "config", "core.hooksPath", str(hooks))
        self.git(root, "config", "commit.gpgsign", "false")
        require(not self.git(root, "status", "--porcelain=v1", "--ignore-submodules=none"), "new clone must be clean")
        return root

    def build(self, root, system, label, *, configure=False, source_changed=False):
        print(f"{system}: {label}", flush=True)
        if system == "scons":
            command = [self.args.scons, "target=template_debug", "arch=" + self.args.architecture, "-j" + str(self.args.jobs)]
        else:
            if configure:
                self.command(label + "-configure", [self.args.cmake, "-S", root, "-B", root / "build/identity-rebuild", "-DCMAKE_BUILD_TYPE=Debug", "-DGODOTCPP_ARCH=" + self.args.architecture, "-DPython3_EXECUTABLE=" + sys.executable], root)
            command = [self.args.cmake, "--build", root / "build/identity-rebuild", "--parallel", str(self.args.jobs)]
        output = self.command(system + "-" + label, command, root)
        if source_changed:
            require("bs_build_info.cpp" in output, f"{system} did not compile the changed identity translation unit")
        candidates = sorted(path for path in (root / "project/bin" / PLATFORMS[sys.platform]).glob("*template_debug*") if path.suffix == SUFFIXES[sys.platform])
        require(len(candidates) == 1, f"expected exactly one ordinary debug artifact, found {candidates}")
        return candidates[0]

    def retain(self, library, system, label):
        directory = self.output / "artifacts" / system / label
        directory.mkdir(parents=True)
        destination = directory / library.name
        shutil.copy2(library, destination)
        return destination

    def inspect(self, root, library, label):
        return json.loads(self.command(label + "-inspect", [sys.executable, root / "scripts/build_metadata.py", "--artifact", library]))

    def query(self, root, library, label, *, strict=False, failure=None):
        command = [sys.executable, root / "scripts/query_build_info.py", "--godot", self.args.godot, "--library", library]
        if strict:
            command.append("--require-current")
        output = self.command(label + ("-strict" if strict else "-diagnostic"), command, root, failure=failure)
        return None if failure else json.loads(output)

    def package(self, root, library, label, *, failure=None, target="template_debug"):
        config = json.loads((root / "build_versions.json").read_text())
        return self.command(label + "-package", [sys.executable, root / "scripts/verify_build_artifacts.py", "--binary-dir", library.parent, "--platform", PLATFORMS[sys.platform], "--architecture", self.args.architecture, "--target", target, "--api", config["godot_api"], "--precision", config["precision"]], root, failure=failure)

    def state(self, root, library, system, label, *, revision, state, dependency):
        config = json.loads((root / "build_versions.json").read_text())
        info = self.inspect(root, library, system + "-" + label)
        expected = {
            "schema": 1, "extension_version": config["extension_version"], "config_sha256": config_sha(config),
            "godot_api": config["godot_api"], "godot_runtime": config["godot_runtime"],
            "source": {"revision": revision, "state": state},
            "godot_cpp": {"revision": dependency, "selected_revision": dependency, "state": "unknown" if dependency == "unknown" else "clean"},
            "build": {"platform": PLATFORMS[sys.platform], "architecture": self.args.architecture, "target": "template_debug", "precision": config["precision"], "native_tests": False},
        }
        require(info == expected, f"{system}-{label}: linked payload differs from independently observed inputs: {info!r} != {expected!r}")
        loaded = self.query(root, library, system + "-" + label)
        require(loaded["build_info"] == info, "loaded library differs from byte-inspected library")
        require(loaded["artifact_sha256"] == sha(library), "loaded library SHA differs from retained bytes")
        require(loaded["checkout_info"]["source"] == expected["source"], "diagnostic checkout state differs")
        require(loaded["godot_version"]["major"] == int(config["godot_api"].split(".")[0]), "actual host Godot version absent")
        self.report["states"].append({"system": system, "label": label, "root": str(root), "artifact": str(library), "sha256": sha(library), "loaded": loaded})
        self.save()
        return info

    def accepted(self, root, library, label):
        self.query(root, library, label, strict=True)
        self.package(root, library, label)

    def rejected(self, root, library, label, reason):
        self.query(root, library, label, strict=True, failure=reason)
        self.package(root, library, label, failure=reason)

    def stale(self, root, library, label, actual, checkout_revision, reason):
        loaded = self.query(root, library, label)
        require(loaded["build_info"] == actual, "diagnostic substituted checkout identity for stale library")
        require(loaded["checkout_info"]["source"]["revision"] == checkout_revision, "stale diagnostic omitted current checkout revision")
        self.rejected(root, library, label, reason)

    def archive(self, root, system):
        # Materialize committed inputs only, including the pinned dependency; no .git or build cache.
        parent = self.output / (system + "-archive-parent")
        parent.mkdir()
        self.git(parent, "init")
        self.git(parent, "-c", "user.name=Build identity regression", "-c", "user.email=build-identity@example.invalid", "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "test: containing checkout is not archive provenance")
        archive = parent / "archive"
        archive.mkdir()
        for source, destination in ((root, archive), (root / "godot-cpp", archive / "godot-cpp")):
            data = subprocess.check_output(["git", "-C", str(source), "archive", "--format=tar", "HEAD"], env=self.env)
            destination.mkdir(exist_ok=True)
            with tarfile.open(fileobj=io.BytesIO(data)) as stream:
                stream.extractall(destination, filter="data")
        require(not (archive / ".git").exists() and not (archive / "godot-cpp/.git").exists(), "archive inherited Git metadata")
        return archive

    def exercise(self, system):
        root = self.clone(system)
        dependency = self.git(root / "godot-cpp", "rev-parse", "HEAD")
        initial_revision = self.git(root, "rev-parse", "HEAD")
        library = self.build(root, system, "clean-A", configure=True)
        retained_a = self.retain(library, system, "A")
        info_a = self.state(root, retained_a, system, "clean-A", revision=initial_revision, state="clean", dependency=dependency)
        self.accepted(root, retained_a, system + "-clean-A")

        # Ignored build/generation noise must preserve clean identity and header timestamps.
        headers = list((root / "build").rglob("bs_build_info.gen.h"))
        require(len(headers) == 1, f"expected one generated identity header, found {headers}")
        header = headers[0]
        header_before = (sha(header), header.stat().st_mtime_ns)
        library_before = sha(library)
        for noise in (root / "build/identity-ignored.txt", root / "godot-cpp/bin/identity-ignored.txt"):
            noise.parent.mkdir(parents=True, exist_ok=True)
            noise.write_text("Ignored generation noise.\n")
        require(not self.git(root, "status", "--porcelain=v1", "--ignore-submodules=none"), "ignored generation noise dirtied source identity")
        library = self.build(root, system, "ignored-no-op")
        require((sha(header), header.stat().st_mtime_ns) == header_before, "no-op build rewrote identity header")
        require(sha(library) == library_before, "ignored generation noise changed linked artifact")
        self.report["states"].append({"system": system, "label": "ignored-no-op", "header_sha256": header_before[0], "header_mtime_ns": header_before[1], "artifact_sha256": library_before})
        self.save()

        # Empty commit changes revision only; neither build system is manually reconfigured afterward.
        self.git(root, "commit", "--allow-empty", "-m", "test: advance identity without changing source bytes")
        revision_b = self.git(root, "rev-parse", "HEAD")
        require(initial_revision != revision_b, "empty commit did not change revision")
        require(not self.git(root, "diff", initial_revision, revision_b, "--"), "revision-only transition changed source bytes")
        self.stale(root, retained_a, system + "-old-A-at-B", info_a, revision_b, "source.revision")
        library = self.build(root, system, "revision-B", source_changed=True)
        retained_b = self.retain(library, system, "B")
        info_b = self.state(root, retained_b, system, "revision-B", revision=revision_b, state="clean", dependency=dependency)
        require(sha(retained_a) != sha(retained_b), "revision-only rebuild left old artifact bytes")
        self.accepted(root, retained_b, system + "-revision-B")

        config_path = root / "build_versions.json"
        config = json.loads(config_path.read_text())
        parts = config["extension_version"].split(".")
        parts[-1] = str(int(parts[-1]) + 1)
        config["extension_version"] = ".".join(parts)
        config_path.write_text(json.dumps(config, indent=2) + "\n")
        self.git(root, "add", "build_versions.json")
        self.git(root, "commit", "-m", "test: change selected extension version")
        revision_c = self.git(root, "rev-parse", "HEAD")
        self.stale(root, retained_b, system + "-old-B-at-C", info_b, revision_c, "extension_version")
        library = self.build(root, system, "config-C", source_changed=True)
        retained_c = self.retain(library, system, "C")
        info_c = self.state(root, retained_c, system, "config-C", revision=revision_c, state="clean", dependency=dependency)
        require(info_c["config_sha256"] != info_b["config_sha256"], "configuration fingerprint did not change")
        self.accepted(root, retained_c, system + "-config-C")

        # Dirty A/B intentionally share revision+state. Strict mode must not claim exact current bytes.
        source = root / "src/bs_build_info.cpp"
        original = source.read_bytes()
        source.write_bytes(original + b"\n// Build identity regression: dirty A.\n")
        dirty_a_source_sha = sha(source)
        library = self.build(root, system, "dirty-A", source_changed=True)
        retained_dirty_a = self.retain(library, system, "dirty-A")
        dirty_info = self.state(root, retained_dirty_a, system, "dirty-A", revision=revision_c, state="dirty", dependency=dependency)
        self.rejected(root, retained_dirty_a, system + "-dirty-A", "source.state")
        source.write_bytes(original + b"\n// Build identity regression: dirty B.\n")
        require(sha(source) != dirty_a_source_sha, "dirty A/B source contents did not differ")
        require(self.git(root, "rev-parse", "HEAD") == revision_c, "dirty A/B changed HEAD")
        self.stale(root, retained_dirty_a, system + "-dirty-A-at-dirty-B", dirty_info, revision_c, "source.state")
        library = self.build(root, system, "dirty-B", source_changed=True)
        retained_dirty_b = self.retain(library, system, "dirty-B")
        self.state(root, retained_dirty_b, system, "dirty-B", revision=revision_c, state="dirty", dependency=dependency)
        self.rejected(root, retained_dirty_b, system + "-dirty-B", "source.state")
        source.write_bytes(original + b"\n// Build identity regression: staged edit.\n")
        self.git(root, "add", "src/bs_build_info.cpp")
        require(bool(self.git(root, "diff", "--cached", "--", "src/bs_build_info.cpp")), "staged edit is absent from the index")
        library = self.build(root, system, "staged-edit", source_changed=True)
        staged = self.retain(library, system, "staged-edit")
        self.state(root, staged, system, "staged-edit", revision=revision_c, state="dirty", dependency=dependency)
        self.rejected(root, staged, system + "-staged-edit", "source.state")
        self.git(root, "restore", "--staged", "src/bs_build_info.cpp")
        source.write_bytes(original)
        require(not self.git(root, "status", "--porcelain=v1", "--ignore-submodules=none"), "source restoration did not restore clean clone")
        library = self.build(root, system, "restored-clean", source_changed=True)
        restored = self.retain(library, system, "restored")
        self.state(root, restored, system, "restored-clean", revision=revision_c, state="clean", dependency=dependency)
        self.accepted(root, restored, system + "-restored-clean")

        # A convincing filename cannot turn stale/debug bytes into current/release bytes.
        wrong_dir = self.output / "artifacts" / system / "misleading"
        wrong_dir.mkdir()
        wrong = wrong_dir / ("barista_script.template_release" + library.suffix)
        shutil.copy2(retained_a, wrong)
        self.stale(root, wrong, system + "-wrong-library", info_a, revision_c, "extension_version")
        shutil.copy2(restored, wrong)
        self.package(root, wrong, system + "-wrong-target", target="template_release", failure="build.target")

        archive = self.archive(root, system)
        library = self.build(archive, system, "archive", configure=True)
        retained_archive = self.retain(library, system, "archive")
        self.state(archive, retained_archive, system, "archive", revision="unknown", state="unknown", dependency="unknown")
        self.rejected(archive, retained_archive, system + "-archive", "source.state")
        require(not self.git(root, "status", "--porcelain=v1", "--ignore-submodules=none"), "completed clone is unexpectedly dirty")

    def run(self):
        try:
            self.report["source_status_before"] = self.git(self.args.source, "status", "--porcelain=v1", "--ignore-submodules=none")
            config = json.loads(self.git(self.args.source, "show", self.args.revision + ":build_versions.json"))
            self.command("python-version", [sys.executable, "--version"])
            scons = self.command("scons-version", [self.args.scons, "--version"])
            require("SCons: v" + config["toolchain"]["scons"] + "." in scons, "regression must use the selected SCons version")
            self.command("cmake-version", [self.args.cmake, "--version"])
            self.command("godot-version", [self.args.godot, "--version"])
            for system in self.args.systems:
                self.exercise(system)
            self.report["status"] = "passed"
        except BaseException as error:
            self.report.update(status="failed", error=f"{type(error).__name__}: {error}")
            raise
        finally:
            self.save()
        # The caller may be used by another worker. Only immutable committed inputs are cloned.
        self.report["source_status_after"] = self.git(self.args.source, "status", "--porcelain=v1", "--ignore-submodules=none")
        self.save()
        print(f"PASS build identity revision/config/dirty/archive/wrong-library checks: {self.output / 'report.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT, help="local repository with initialized pinned godot-cpp; never modified")
    parser.add_argument("--revision", default="HEAD", help="committed candidate to clone (uncommitted edits are never copied)")
    parser.add_argument("--godot", type=Path, required=True)
    parser.add_argument("--scons", default="scons")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--architecture", default={"aarch64": "arm64", "AMD64": "x86_64"}.get(platform.machine(), platform.machine()))
    parser.add_argument("--systems", choices=("scons", "cmake"), nargs="+", default=["scons", "cmake"])
    parser.add_argument("--output", type=Path, help="new directory for isolated clones, logs, and retained artifacts")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 4:
        parser.error("--jobs must be between 1 and 4")
    if sys.platform not in PLATFORMS:
        parser.error("real loaded-library checks require a supported desktop host")
    args.source = args.source.resolve()
    args.godot = args.godot.resolve()
    args.revision = subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "--verify", args.revision + "^{commit}"], text=True).strip()
    if args.output is None:
        args.output = Path(tempfile.mkdtemp(prefix="barista-build-identity-")) / "evidence"
    if args.output.resolve() == args.source or args.source in args.output.resolve().parents:
        parser.error("--output must be outside the caller source tree")
    if args.output.exists():
        parser.error("--output must not already exist")
    Regression(args).run()


if __name__ == "__main__":
    main()
