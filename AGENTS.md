# Repository Guidelines

## Project Structure & Module Organization

BaristaScript is a C++17 GDExtension that makes Godot 4.7 recognize `.barista` files as non-executable `Script` resources. Core code lives in `src/`: `barista_script.*` defines the resource, `barista_script_language.*` exposes language metadata, `barista_script_resource_loader.*` handles files, and `register_types.*` manages extension startup. The `project/` directory is the Godot fixture; its `example.barista` file and `tests/smoke_test.gd` verify editor recognition. CI-specific validation lives in `tests/validate_ci.py`. Treat `godot-cpp/` as a pinned submodule, not vendored project code.

## Build, Test, and Development Commands

- `git submodule update --init --recursive` initializes the required bindings.
- `scons api_version=4.7 target=template_debug` builds the debug extension into `project/bin/<platform>/`.
- `scons api_version=4.7 target=template_release` creates a release build.
- `cmake -S . -B build -DGODOTCPP_API_VERSION=4.7 -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel` exercises the alternative CMake path.
- `python3 tests/validate_ci.py` checks that the CI matrix matches the pinned API precision and event policy.
- `godot --headless --path project --editor --quit` imports the fixture; then `python3 tests/run_gdscript_suites.py --godot $(which godot)` runs every GDScript suite in `project/tests/` and fails when one did not actually run.

Invoke Python tooling as `python3`; a bare `python` is not present on every supported development
machine, and `.pre-commit-config.yaml` already uses `python3`.

## Coding Style & Naming Conventions

Follow `.clang-format`: tabs with width 4, attached braces, and Godot-style C++. Use PascalCase for classes (`BaristaScriptLanguage`), snake_case for methods and files, and leading underscores for Godot extension overrides. Format C++ with `clang-format -i src/*.cpp src/*.h`; check without edits using `clang-format --dry-run --Werror src/*.cpp src/*.h`. GDScript also uses tabs and typed declarations where practical.

## Testing Guidelines

Add focused assertions to `project/tests/smoke_test.gd` for runtime-facing behavior. Name new GDScript tests `*_test.gd` and repository checks `test_*.py` or `validate_*.py`. A `*_test.gd` file under `project/tests/` is discovered and run by `tests/run_gdscript_suites.py` with no further wiring, and it must end with `quit(SuiteGuard.report("<suite stem>", failures))` using `project/tests/suite_guard.gd`: SceneTree quits 0 on a parse error, so the shared `BS_SUITE_OK <stem>` sentinel is the only evidence the suite ran, and the runner fails the job without it. Declare a suite that needs arguments, or a script not named `*_test.gd`, in `tests/gdscript_suites.json`. Run CI validation, an affected build path, editor import, and the suite runner before opening a PR. No numeric coverage threshold is enforced; behavioral changes should include regression coverage.

## Commit & Pull Request Guidelines

History uses short, imperative Conventional Commit subjects such as `feat:`, `fix:`, and `ci:`. Keep commits scoped and explain intent in the body when needed. PRs should summarize behavior, list exact verification commands, link relevant issues, and include screenshots only for visible editor changes. Require green CI and call out platform or build-matrix changes explicitly.

## Licensing Headers

The project is MIT licensed to Cafecito Games LLC. Every C++, Python, and GDScript source file carries the header produced by `scripts/add_license_header.py`; run `python3 scripts/add_license_header.py` to fill in any missing headers, or `python3 scripts/add_license_header.py --check` to report them without editing. `prek install` wires the `.pre-commit-config.yaml` hook that adds the header at commit time, failing the commit when it had to modify a file so the change can be re-staged. Third-party and generated trees (`godot-cpp/`, `build/`, `bin/`, `src/gen/`) and the upstream-derived `methods.py` are excluded.

## Generated Files

Do not commit platform libraries, `build/`, or incidental Godot cache files. Preserve the tracked `project/.godot/extension_list.cfg`, which is required for first-scan language registration.
