# Repository Guidelines

## Project Structure & Module Organization

BaristaScript is a C++17 GDExtension that makes Godot 4.7 recognize `.barista` files as non-executable `Script` resources. Core code lives in `src/`: `barista_script.*` defines the resource, `barista_script_language.*` exposes language metadata, `barista_script_resource_loader.*` handles files, and `register_types.*` manages extension startup. The `project/` directory is the Godot fixture; its `example.barista` file and `tests/smoke_test.gd` verify editor recognition. CI-specific validation lives in `tests/validate_ci.py`. Treat `godot-cpp/` as a pinned submodule, not vendored project code.

## Build, Test, and Development Commands

- `git submodule update --init --recursive` initializes the required bindings.
- `scons api_version=4.7 target=template_debug` builds the debug extension into `project/bin/<platform>/`.
- `scons api_version=4.7 target=template_release` creates a release build.
- `cmake -S . -B build -DGODOTCPP_API_VERSION=4.7 -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel` exercises the alternative CMake path.
- `python tests/validate_ci.py` checks that the CI matrix matches the pinned API precision and event policy.
- `godot --headless --path project --editor --quit` imports the fixture; then `godot --headless --path project --script res://tests/smoke_test.gd` runs the recognition smoke test.

## Coding Style & Naming Conventions

Follow `.clang-format`: tabs with width 4, attached braces, and Godot-style C++. Use PascalCase for classes (`BaristaScriptLanguage`), snake_case for methods and files, and leading underscores for Godot extension overrides. Format C++ with `clang-format -i src/*.cpp src/*.h`; check without edits using `clang-format --dry-run --Werror src/*.cpp src/*.h`. GDScript also uses tabs and typed declarations where practical.

## Testing Guidelines

Add focused assertions to `project/tests/smoke_test.gd` for runtime-facing behavior. Name new GDScript tests `*_test.gd` and repository checks `test_*.py` or `validate_*.py`. Run CI validation, an affected build path, editor import, and the smoke test before opening a PR. No numeric coverage threshold is enforced; behavioral changes should include regression coverage.

## Commit & Pull Request Guidelines

History uses short, imperative Conventional Commit subjects such as `feat:`, `fix:`, and `ci:`. Keep commits scoped and explain intent in the body when needed. PRs should summarize behavior, list exact verification commands, link relevant issues, and include screenshots only for visible editor changes. Require green CI and call out platform or build-matrix changes explicitly.

## Generated Files

Do not commit platform libraries, `build/`, or incidental Godot cache files. Preserve the tracked `project/.godot/extension_list.cfg`, which is required for first-scan language registration.
