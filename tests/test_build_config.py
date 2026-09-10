#!/usr/bin/env python3
# test_build_config.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Configuration, compiled-payload and provenance tests in disposable source trees."""

import copy
import json
import os
import platform
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_config
import build_metadata
import verify_build_artifacts


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="barista-config-")
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "build_versions.json"
        self.config = json.loads((ROOT / "build_versions.json").read_text())

    def load(self, value=None):
        self.path.write_text(json.dumps(self.config if value is None else value))
        return build_config.load_config(self.path)

    def test_preserves_current_defaults_and_explicit_overrides(self):
        config = self.load()
        self.assertEqual(config["extension_version"], "0.1.0")
        self.assertEqual(config["godot_api"], "4.7")
        self.assertEqual(config["godot_runtime"], "4.7.2")
        self.assertEqual(build_config.effective_tools(config)["emscripten"], "4.0.11")
        self.assertEqual(build_config.effective_tools(config, "web")["emscripten"], "3.1.62")
        self.assertEqual(build_config.effective_tools(config)["windows_compiler"], "mingw")
        self.assertEqual(build_config.effective_tools(config, "windows")["windows_compiler"], "msvc")
        self.assertEqual(build_config.effective_tools(config, "linux")["scons"], "4.4.0")

    def test_missing_malformed_duplicate_and_non_json_values_fail(self):
        with self.assertRaisesRegex(ValueError, "configuration"):
            build_config.load_config(self.path)
        for content in ('{', '{"schema":1,"schema":1}', '{"schema":NaN}'):
            with self.subTest(content=content):
                self.path.write_text(content)
                with self.assertRaises(ValueError):
                    build_config.load_config(self.path)

    def test_schema_and_unknown_keys_fail(self):
        for key, value in (("schema", True), ("schema", 2), ("godot_api", 4.7),
                           ("godot_api", "5.0"), ("godot_runtime", "4.8.2"),
                           ("precision", "double"), ("extension_version", "../../bad")):
            with self.subTest(key=key, value=value):
                config = dict(self.config, **{key: value})
                with self.assertRaisesRegex(ValueError, key):
                    self.load(config)
        for config in ({k: v for k, v in self.config.items() if k != "godot_api"},
                       dict(self.config, source_revision="not a version")):
            with self.assertRaisesRegex(ValueError, "keys"):
                self.load(config)

    def test_override_is_explicit_and_rejects_unsupported_keys(self):
        config = self.load()
        with self.assertRaisesRegex(ValueError, "emscripten.*expected.*3.1.62.*actual.*4.0.11"):
            build_config.effective_tools(config, "web", {"emscripten": "4.0.11"})
        self.assertEqual(build_config.effective_tools(config, "web", {"emscripten": "3.1.62"})["emscripten"], "3.1.62")
        bad = copy.deepcopy(config)
        bad["platform_overrides"]["linux"] = {"unrecognized": "1.0.0"}
        with self.assertRaisesRegex(ValueError, "override"):
            self.load(bad)

    def test_actual_target_settings_are_validated_without_host_inference(self):
        config = self.load()
        for platform, architecture in (("windows", "x86_32"), ("android", "arm64"),
                                       ("web", "wasm32"), ("macos", "universal"),
                                       ("macos", "arm64"), ("ios", "arm64")):
            selected = build_config.validate_selection(config, platform=platform, architecture=architecture,
                                                       target="template_release", api="4.7", precision="single")
            self.assertEqual(selected["architecture"], architecture)
            self.assertEqual(selected["platform"], platform)
        values = dict(platform="linux", architecture="x86_64", target="template_debug", api="4.7", precision="single")
        for key, value in (("platform", "other"), ("architecture", "universal"),
                           ("target", "editor"), ("api", "4.6"), ("precision", "double")):
            with self.subTest(key=key):
                with self.assertRaisesRegex(ValueError, "expected.*actual"):
                    build_config.validate_selection(config, **dict(values, **{key: value}))

    def test_api_path_and_header_are_derived_and_checked(self):
        config = self.load()
        path = build_config.api_path(config, Path(self.temp.name))
        self.assertEqual(path.name, "extension_api-4-7.json")
        header = build_config.expected_api_header(config)
        build_config.validate_api_header(config, header)
        for key in header:
            bad = dict(header, **{key: None})
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, key):
                build_config.validate_api_header(config, bad)

    def test_canonical_fingerprint_is_order_independent_and_tracks_version(self):
        config = self.load()
        self.assertEqual(build_config.config_fingerprint(config), build_config.config_fingerprint(dict(reversed(list(config.items())))))
        changed = dict(config, extension_version="0.1.1")
        self.assertNotEqual(build_config.config_fingerprint(config), build_config.config_fingerprint(changed))


    def test_cli_projections_and_consumer_disagreement(self):
        self.load()
        command = [sys.executable, str(ROOT / "scripts/build_config.py"), "--config", str(self.path)]
        result = subprocess.run(command + ["--platform", "web", "--format", "github"], capture_output=True, text=True, check=True)
        self.assertIn("emscripten=3.1.62\n", result.stdout)
        cmake = subprocess.run(command + ["--format", "cmake"], capture_output=True, text=True, check=True)
        self.assertIn('set(BARISTA_GODOT_API "4.7")', cmake.stdout)
        for arguments in (("--expect-api", "4.6"), ("--expect-precision", "double"), ("--get", "unknown")):
            with self.subTest(arguments=arguments):
                failed = subprocess.run(command + list(arguments), capture_output=True, text=True)
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn("expected", failed.stderr)
                self.assertEqual(failed.stdout, "")


class MetadataTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="barista-metadata-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "source"
        self.root.mkdir()
        self.environment = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
        self.config = build_config.load_config()
        self.selection = dict(platform="linux", architecture="x86_64", target="template_debug", api="4.7", precision="single")
        self.git(self.root, "init", "-q")
        self.dependency = self.root / "godot-cpp"
        self.dependency.mkdir()
        self.git(self.dependency, "init", "-q")
        (self.dependency / "source.cpp").write_text("int dependency = 1;\n")
        self.commit(self.dependency)
        (self.root / ".gitignore").write_text("build/\n.godot/\n")
        (self.root / "source.cpp").write_text("int extension = 1;\n")
        self.commit(self.root)

    def git(self, root, *arguments):
        return subprocess.run(["git", "-c", "user.name=Build Identity Test", "-c", "user.email=build-test@example.invalid",
                               "-C", str(root), *arguments], env=self.environment, check=True, capture_output=True, text=True).stdout.strip()

    def commit(self, root, empty=False):
        self.git(root, "add", "--all")
        self.git(root, "commit", "-qm", "fixture", *(('--allow-empty',) if empty else ()))
        return self.git(root, "rev-parse", "HEAD")

    def metadata(self, root=None, config=None):
        return build_metadata.create_metadata(root or self.root, config or self.config, self.selection, native_tests=False)

    def test_clean_revision_and_dependency_gitlink_are_distinct_authorities(self):
        value = self.metadata()
        self.assertEqual(value["source"], {"revision": self.git(self.root, "rev-parse", "HEAD"), "state": "clean"})
        dep = self.git(self.dependency, "rev-parse", "HEAD")
        self.assertEqual(value["godot_cpp"], {"revision": dep, "selected_revision": dep, "state": "clean"})
        self.assertEqual(value["build"]["architecture"], "x86_64")
        self.assertEqual(value["build"]["platform"], "linux")
        self.assertFalse(value["build"]["native_tests"])
        build_metadata.verify_identity(value, value, release=True)

    def test_tracked_working_staged_untracked_and_ignored_states(self):
        path = self.root / "source.cpp"
        original = path.read_bytes()
        path.write_bytes(original + b"// dirty\n")
        self.assertEqual(self.metadata()["source"]["state"], "dirty")
        self.git(self.root, "add", "source.cpp")
        self.assertEqual(self.metadata()["source"]["state"], "dirty")
        self.git(self.root, "restore", "--staged", "source.cpp")
        path.write_bytes(original)
        self.assertEqual(self.metadata()["source"]["state"], "clean")
        (self.root / "build").mkdir()
        (self.root / "build/generated.cpp").write_text("ignored output")
        self.assertEqual(self.metadata()["source"]["state"], "clean")
        (self.root / "new.cpp").write_text("new compilation input")
        self.assertEqual(self.metadata()["source"]["state"], "dirty")
        self.git(self.root, "add", "-f", "build/generated.cpp")
        (self.root / "new.cpp").unlink()
        self.commit(self.root)
        (self.root / "build/generated.cpp").write_text("tracked ignored-path edit must remain dirty")
        self.assertEqual(self.metadata()["source"]["state"], "dirty")

    def test_strict_current_query_rejects_two_different_dirty_contents_at_one_head(self):
        import query_build_info
        source = self.root / "source.cpp"
        source.write_text("int content_a = 1;\n")
        built_a = self.metadata()
        source.write_text("int content_b = 2;\n")
        current_b = self.metadata()
        self.assertEqual(built_a, current_b, "revision/state alone cannot prove current dirty content")
        with self.assertRaisesRegex(ValueError, "clean"):
            query_build_info.require_current(built_a, self.root, self.config)
        self.git(self.root, "add", "source.cpp")
        with self.assertRaisesRegex(ValueError, "clean"):
            query_build_info.require_current(built_a, self.root, self.config)
        self.git(self.root, "restore", "--staged", "source.cpp")
        self.git(self.root, "restore", "source.cpp")
        query_build_info.require_current(self.metadata(), self.root, self.config)

    def test_dependency_mismatch_and_dirty_checkout_are_reported_and_rejected(self):
        selected = self.metadata()["godot_cpp"]["selected_revision"]
        actual = self.commit(self.dependency, empty=True)
        value = self.metadata()
        self.assertEqual(value["godot_cpp"]["selected_revision"], selected)
        self.assertEqual(value["godot_cpp"]["revision"], actual)
        self.assertNotEqual(actual, selected)
        with self.assertRaises(ValueError):
            build_metadata.verify_identity(value, value, release=True)
        self.git(self.root, "add", "godot-cpp")
        self.commit(self.root)
        (self.dependency / "source.cpp").write_text("dirty dependency")
        value = self.metadata()
        self.assertEqual(value["godot_cpp"]["state"], "dirty")
        with self.assertRaisesRegex(ValueError, "clean"):
            build_metadata.verify_identity(value, value, release=True)

    def test_archive_nested_in_another_repository_is_honestly_unknown(self):
        archive = self.root / "archive"
        shutil.copytree(self.root, archive, ignore=shutil.ignore_patterns(".git", "archive"))
        value = self.metadata(archive)
        self.assertEqual(value["source"], {"revision": "unknown", "state": "unknown"})
        self.assertEqual(value["godot_cpp"], {"revision": "unknown", "selected_revision": "unknown", "state": "unknown"})
        with self.assertRaisesRegex(ValueError, "unknown|clean"):
            build_metadata.verify_identity(value, value, release=True)
        bad = dict(self.config, godot_api="invalid")
        with self.assertRaises(ValueError):
            self.metadata(archive, bad)

    def test_missing_gitlink_or_broken_git_metadata_is_not_archive_success(self):
        self.git(self.root, "rm", "--cached", "godot-cpp")
        with self.assertRaisesRegex(ValueError, "gitlink"):
            self.metadata()
        (self.root / ".git").rename(self.root / ".broken-git")
        (self.root / ".git").write_text("gitdir: missing-directory\n")
        with self.assertRaisesRegex(ValueError, "Git"):
            self.metadata()

    def test_header_is_stable_and_revision_or_config_changes_replace_it(self):
        header = self.root / "build/generated/bs_build_info.gen.h"
        value = self.metadata()
        self.assertTrue(build_metadata.write_header(header, value))
        original = (header.read_bytes(), header.stat().st_mtime_ns)
        self.assertFalse(build_metadata.write_header(header, value))
        self.assertEqual((header.read_bytes(), header.stat().st_mtime_ns), original)
        self.commit(self.root, empty=True)
        newer = self.metadata()
        self.assertNotEqual(value["source"]["revision"], newer["source"]["revision"])
        self.assertTrue(build_metadata.write_header(header, newer))
        self.assertNotEqual(header.read_bytes(), original[0])
        changed = self.metadata(config=dict(self.config, extension_version="0.1.1"))
        self.assertTrue(build_metadata.write_header(header, changed))
        self.assertNotEqual(newer["config_sha256"], changed["config_sha256"])
        self.assertNotIn(str(self.root), header.read_text())
        self.assertNotIn("timestamp", header.read_text())
        with self.assertRaisesRegex(ValueError, "source.revision"):
            build_metadata.verify_identity(value, newer)
        with self.assertRaisesRegex(ValueError, "extension_version|config_sha256"):
            build_metadata.verify_identity(newer, changed)

    def test_actual_payload_inspector_checks_framing_canonical_json_and_duplicates(self):
        value = self.metadata()
        record = build_metadata.encode_envelope(value)
        self.assertEqual(build_metadata.inspect_bytes(b"binary-prefix" + record + b"binary-suffix"), value)
        self.assertEqual(build_metadata.inspect_bytes(record + b"other architecture" + record), value)
        other = build_metadata.encode_envelope(self.metadata(config=dict(self.config, extension_version="0.1.1")))
        for content in (b"no metadata", record[:-1], record.replace(b'"clean"', b'"dirty"'), record + other,
                        record + build_metadata.ENVELOPE_PREFIX + b"broken"):
            with self.subTest(content=content[:60]), self.assertRaises(ValueError):
                build_metadata.inspect_bytes(content)
        bad = copy.deepcopy(value)
        bad["source"]["revision"] = "unknown"
        with self.assertRaisesRegex(ValueError, "source"):
            build_metadata.encode_envelope(bad)
        bad = dict(value, machine_path="not allowed")
        with self.assertRaisesRegex(ValueError, "keys"):
            build_metadata.encode_envelope(bad)

    def test_generated_payload_survives_actual_linking_and_compiler_rejects_wrong_architecture(self):
        compiler = shutil.which("c++")
        if not compiler or sys.platform not in ("darwin", "linux"):
            self.skipTest("host C++ compiler check requires macOS or Linux")
        architecture = {"aarch64": "arm64", "arm64": "arm64", "x86_64": "x86_64"}[platform.machine()]
        selection = dict(self.selection, platform="macos" if sys.platform == "darwin" else "linux", architecture=architecture)
        metadata = build_metadata.create_metadata(self.root, self.config, selection, native_tests=False)
        header = self.root / "build/bs_build_info.gen.h"
        build_metadata.write_header(header, metadata)
        source = self.root / "build/payload.cpp"
        source.write_text('#include "bs_build_info.gen.h"\n#include <cstdio>\nint main() { return std::puts(BS_BUILD_INFO_ENVELOPE) < 0; }\n')
        artifact = self.root / "build/payload"
        command = [compiler, "-std=c++17", "-DDEBUG_ENABLED", str(source), "-o", str(artifact)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(build_metadata.inspect_artifact(artifact), metadata)
        runtime = subprocess.run([str(artifact)], check=True, capture_output=True)
        self.assertEqual(build_metadata.inspect_bytes(runtime.stdout), metadata)
        wrong = copy.deepcopy(metadata)
        wrong["build"]["architecture"] = "x86_64" if architecture == "arm64" else "arm64"
        build_metadata.write_header(header, wrong)
        failed = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("architecture", failed.stderr)

    def test_each_expected_build_field_is_checked_and_native_is_distinct(self):
        value = self.metadata()
        changes = {"architecture": "x86_32", "platform": "windows", "target": "template_release", "native_tests": True}
        for field, actual in changes.items():
            changed = copy.deepcopy(value)
            changed["build"][field] = actual
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "build." + field):
                build_metadata.verify_identity(changed, value)
        native = build_metadata.create_metadata(self.root, self.config, self.selection, native_tests=True)
        self.assertTrue(native["build"]["native_tests"])


    def test_cli_inspects_old_artifact_without_replacing_its_identity(self):
        value = self.metadata()
        artifact = self.root / "build/library.so"
        artifact.parent.mkdir()
        artifact.write_bytes(build_metadata.encode_envelope(value))
        config = self.root / "build/versions.json"
        config.write_text(json.dumps(self.config))
        command = [sys.executable, str(ROOT / "scripts/build_metadata.py"), "--root", str(self.root), "--artifact", str(artifact)]
        new_revision = self.commit(self.root, empty=True)
        inspected = subprocess.run(command, check=True, capture_output=True, text=True)
        self.assertEqual(json.loads(inspected.stdout), value)
        self.assertNotEqual(json.loads(inspected.stdout)["source"]["revision"], new_revision)
        selected = ["--config", str(config), "--platform", "linux", "--architecture", "x86_64",
                    "--target", "template_debug", "--api", "4.7", "--precision", "single"]
        failed = subprocess.run(command + selected + ["--verify", "--release"], capture_output=True, text=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn("source.revision", failed.stderr)
        release_without_comparison = subprocess.run(command + ["--release"], capture_output=True, text=True)
        self.assertNotEqual(release_without_comparison.returncode, 0)
        self.assertIn("--verify", release_without_comparison.stderr)
        artifact.write_bytes(build_metadata.encode_envelope(self.metadata()))
        passed = subprocess.run(command + selected + ["--verify", "--release"], capture_output=True, text=True)
        self.assertEqual(passed.returncode, 0, passed.stdout + passed.stderr)


    def test_packaging_checks_every_actual_library_and_fails_for_empty_stale_or_incomplete_artifacts(self):
        directory = self.root / "build/package"
        directory.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "no.*artifacts"):
            verify_build_artifacts.verify_artifacts(directory, self.root, self.config, self.selection)
        library = directory / "libbarista_script.linux.template_debug.x86_64.so"
        library.write_bytes(build_metadata.encode_envelope(self.metadata()))
        records = verify_build_artifacts.verify_artifacts(directory, self.root, self.config, self.selection)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["build_info"], self.metadata())
        self.assertEqual(len(records[0]["sha256"]), 64)
        bad = directory / "stale-unselected.dll"
        bad.write_bytes(b"not a compiled identity")
        with self.assertRaisesRegex(ValueError, "stale-unselected.dll.*absent"):
            verify_build_artifacts.verify_artifacts(directory, self.root, self.config, self.selection)
        bad.unlink()
        self.commit(self.root, empty=True)
        with self.assertRaisesRegex(ValueError, "source.revision"):
            verify_build_artifacts.verify_artifacts(directory, self.root, self.config, self.selection)
        library.write_bytes(build_metadata.encode_envelope(self.metadata()))
        (self.root / "source.cpp").write_text("dirty source")
        with self.assertRaisesRegex(ValueError, "clean"):
            verify_build_artifacts.verify_artifacts(directory, self.root, self.config, self.selection)



    def test_universal_slice_parser_rejects_malformed_bounds_and_cpu_claims_on_every_host(self):
        metadata = build_metadata.create_metadata(self.root, self.config, dict(self.selection, platform="macos", architecture="universal"), native_tests=False)
        payload = build_metadata.encode_envelope(metadata)
        cpus = (0x01000007, 0x0100000c)
        slices = [struct.pack("<IiiIIIII", 0xfeedfacf, cpu, 0, 6, 0, 0, 0, 0) + payload for cpu in cpus]
        for wide in (False, True):
            with self.subTest(wide=wide):
                layout = ">iiQQII" if wide else ">iiIII"
                entry_size = struct.calcsize(layout)
                first = 128
                second = first + ((len(slices[0]) + 63) // 64) * 64
                content = bytearray(second + len(slices[1]))
                content[:8] = struct.pack(">II", 0xcafebabf if wide else 0xcafebabe, 2)
                for index, offset in enumerate((first, second)):
                    values = (cpus[index], 0, offset, len(slices[index]), 6) + ((0,) if wide else ())
                    struct.pack_into(layout, content, 8 + index * entry_size, *values)
                    content[offset:offset + len(slices[index])] = slices[index]
                self.assertEqual(build_metadata.inspect_artifact_bytes(bytes(content)), metadata)
                # Reject an incomplete table, duplicate/misreported CPUs, overlap, and out-of-file slices.
                mutations = ((4, ">I", 1), (8 + entry_size, ">i", cpus[0]),
                             (first + 4, "<i", cpus[1]), (8 + entry_size + 8, ">Q" if wide else ">I", first),
                             (8 + 8, ">Q" if wide else ">I", len(content) + 64))
                for offset, encoding, actual in mutations:
                    bad = bytearray(content)
                    struct.pack_into(encoding, bad, offset, actual)
                    with self.subTest(offset=offset), self.assertRaises(ValueError):
                        build_metadata.inspect_artifact_bytes(bytes(bad))
                with self.assertRaises(ValueError):
                    build_metadata.inspect_artifact_bytes(bytes(content[:-1]))

    @unittest.skipUnless(sys.platform == "darwin" and shutil.which("c++") and shutil.which("lipo"),
                         "real universal-library controls require the macOS toolchain")
    def test_real_universal_requires_both_physical_slices_with_matching_identity(self):
        selected = dict(self.selection, platform="macos", architecture="universal")
        metadata = build_metadata.create_metadata(self.root, self.config, selected, native_tests=False)
        package = self.root / "build/universal-package"
        package.mkdir(parents=True)
        slices = self.root / "build/slices"
        slices.mkdir()
        artifact = package / "libbarista_script.macos.template_debug.dylib"

        def compile_slice(architecture, name, value):
            source = slices / (name + ".cpp")
            library = slices / (name + ".dylib")
            if value is None:
                source.write_text('extern "C" int without_metadata() { return 42; }\n')
            else:
                build_metadata.write_header(slices / (name + ".h"), value)
                source.write_text('#include "' + name + '.h"\nextern "C" const char *build_info() { return BS_BUILD_INFO_ENVELOPE; }\n')
            completed = subprocess.run(["c++", "-std=c++17", "-dynamiclib", "-DDEBUG_ENABLED", "-arch", architecture,
                                        str(source), "-o", str(library)], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            return library

        def combine(first, second):
            subprocess.run(["lipo", "-create", str(first), str(second), "-output", str(artifact)], check=True, capture_output=True)
            self.assertEqual(set(subprocess.check_output(["lipo", "-archs", str(artifact)], text=True).split()), {"arm64", "x86_64"})

        arm = compile_slice("arm64", "arm", metadata)
        x86 = compile_slice("x86_64", "x86", metadata)
        shutil.copy2(arm, artifact)
        self.assertEqual(subprocess.check_output(["lipo", "-archs", str(artifact)], text=True).strip(), "arm64")
        for inspect in (lambda: build_metadata.inspect_artifact(artifact),
                        lambda: verify_build_artifacts.verify_artifacts(package, self.root, self.config, selected)):
            with self.assertRaisesRegex(ValueError, "universal.*slices"):
                inspect()
        combine(arm, x86)
        self.assertEqual(build_metadata.inspect_artifact(artifact), metadata)
        self.assertEqual(len(verify_build_artifacts.verify_artifacts(package, self.root, self.config, selected)), 1)
        bare_x86 = compile_slice("x86_64", "bare", None)
        combine(arm, bare_x86)
        with self.assertRaisesRegex(ValueError, "slice.*absent"):
            verify_build_artifacts.verify_artifacts(package, self.root, self.config, selected)
        changed = copy.deepcopy(metadata)
        changed["extension_version"] = "0.1.1"
        changed_x86 = compile_slice("x86_64", "changed", changed)
        combine(arm, changed_x86)
        with self.assertRaisesRegex(ValueError, "conflicting|match"):
            verify_build_artifacts.verify_artifacts(package, self.root, self.config, selected)


if __name__ == "__main__":
    unittest.main()
