# Debug-only Parse-cache Binding Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the test-oriented `BaristaScriptParseCache` ClassDB surface and symbols from release artifacts while preserving it in debug builds.

**Architecture:** Treat `BaristaScriptParseCache` as a debug probe by guarding its declaration, implementation, include, and registration with `DEBUG_ENABLED`. Verify the build-policy boundary by scanning compiled artifacts for the adapter's unique class marker, with opposite expectations for debug and release targets.

**Tech Stack:** C++17, godot-cpp/GDExtension, SCons, CMake, Python 3, GitHub Actions.

---

### Task 1: Add the artifact-level regression check

**Files:**
- Create: `tests/verify_parse_cache_surface.py`
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Write the artifact verifier**

Create a licensed Python script that accepts `--binary-dir` and `--target-type`, finds build artifacts containing the requested target name, and asserts that at least one debug artifact contains `BaristaScriptParseCache` while no release artifact contains it. Print the inspected paths and a clear success or failure reason.

- [ ] **Step 2: Wire the verifier after CI compilation**

Add this unconditional step after the SCons build:

```yaml
      - name: Verify parse-cache release surface
        shell: bash
        run: >-
          python3 tests/verify_parse_cache_surface.py
          --binary-dir project/bin/${{ matrix.target.platform }}
          --target-type ${{ matrix.target-type }}
```

- [ ] **Step 3: Build the unchanged release artifact and verify RED**

Run:

```bash
scons api_version=4.7 target=template_release
python3 tests/verify_parse_cache_surface.py --binary-dir project/bin/macos --target-type template_release
```

Expected: the verifier exits non-zero and reports `BaristaScriptParseCache` in the release artifact.

- [ ] **Step 4: Commit the failing regression check**

```bash
git add .github/workflows/ci.yml tests/verify_parse_cache_surface.py
git commit -m "test: assert parse cache adapter build surface"
```

### Task 2: Make the adapter debug-only

**Files:**
- Modify: `src/barista_script_parse_cache.h`
- Modify: `src/barista_script_parse_cache.cpp`
- Modify: `src/register_types.cpp`

- [ ] **Step 1: Guard the adapter declaration and implementation**

Wrap the adapter class declaration and the implementation translation unit's adapter-specific code in `#ifdef DEBUG_ENABLED` / `#endif // DEBUG_ENABLED`. Keep the licensing headers and includes valid in both build modes.

- [ ] **Step 2: Guard registration and classify the adapter as a probe**

Conditionally include `barista_script_parse_cache.h` in `register_types.cpp`, remove the unconditional registration, and register `BaristaScriptParseCache` inside the existing debug-probe block. Keep `BSParserRef` unconditionally registered.

- [ ] **Step 3: Verify GREEN for release and debug artifacts**

Run:

```bash
scons api_version=4.7 target=template_release
python3 tests/verify_parse_cache_surface.py --binary-dir project/bin/macos --target-type template_release
scons api_version=4.7 target=template_debug
python3 tests/verify_parse_cache_surface.py --binary-dir project/bin/macos --target-type template_debug
```

Expected: both verifier invocations exit zero; the release marker is absent and the debug marker is present.

- [ ] **Step 4: Run focused runtime regression coverage**

Run:

```bash
godot --headless --path project --editor --quit
python3 tests/run_gdscript_suites.py --godot "$(which godot)"
```

Expected: every discovered suite prints its `BS_SUITE_OK` sentinel and the runner exits zero.

- [ ] **Step 5: Commit the implementation**

```bash
git add src/barista_script_parse_cache.cpp src/barista_script_parse_cache.h src/register_types.cpp
git commit -m "fix: hide parse cache adapter from release builds"
```

### Task 3: Run full repository verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Verify both SCons target types and their surfaces**

```bash
scons api_version=4.7 target=template_debug
python3 tests/verify_parse_cache_surface.py --binary-dir project/bin/macos --target-type template_debug
scons api_version=4.7 target=template_release
python3 tests/verify_parse_cache_surface.py --binary-dir project/bin/macos --target-type template_release
```

- [ ] **Step 2: Verify the CMake debug build**

```bash
cmake -S . -B build -DGODOTCPP_API_VERSION=4.7 -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

- [ ] **Step 3: Verify repository checks and runtime suites**

```bash
python3 tests/validate_ci.py
godot --headless --path project --editor --quit
python3 tests/run_gdscript_suites.py --godot "$(which godot)"
python3 scripts/add_license_header.py --check
clang-format --dry-run --Werror src/*.cpp src/*.h
```

- [ ] **Step 4: Inspect the committed diff**

```bash
git status --short
git diff --check origin/main...HEAD
git diff --stat origin/main...HEAD
git diff origin/main...HEAD
```

Expected: the worktree is clean, no whitespace errors are reported, and every change belongs to issue #54.
