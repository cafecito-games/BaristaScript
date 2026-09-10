# BaristaScript

BaristaScript is an experimental scripting language for Godot. This repository
provides Godot 4.7 integration for `.barista` files as non-executable `Script`
resources, with parsing and static analysis.

The project is based on the official
[`godot-cpp-template`](https://github.com/godotengine/godot-cpp-template) and
uses `godot-cpp` 10.0.0-rc2 with the Godot 4.7 extension API.

## Current capabilities

- Registers `BaristaScript` with Godot's scripting-language registry.
- Recognizes and loads `.barista` source files.
- Preserves source text in a `BaristaScript` resource.
- Parses and statically analyzes source, with editor diagnostics and global declaration metadata.

Execution, script instances, completion, debugging, and profiling are not implemented.
See the [API reference contributor guide](docs/api-reference.md) to build the
reference, inspect editor help, and read the current API limitations from its
authored class XML.

## Prerequisites

- Godot 4.7 or a compatible 4.7 patch release.
- A C++17 compiler supported by `godot-cpp`.
- Python 3 and SCons for the primary build.
- CMake 3.17 or newer for the alternative build.

## Clone

Clone recursively so the pinned `godot-cpp` submodule is available:

```sh
git clone --recursive git@github.com:cafecito-games/BaristaScript.git
cd BaristaScript
```

For an existing clone:

```sh
git submodule update --init --recursive
```

## Build with SCons

Build and copy a debug library into the sample Godot project:

```sh
scons api_version=4.7 target=template_debug
```

For a release build:

```sh
scons api_version=4.7 target=template_release
```

Generate a compilation database while building:

```sh
scons api_version=4.7 target=template_debug compiledb=yes
```

## Build with CMake

```sh
cmake -S . -B build \
  -DGODOTCPP_API_VERSION=4.7 \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

Both build systems place the platform library under `project/bin/<platform>/`,
where `project/bin/barista_script.gdextension` can load it.

## Verify editor recognition

Replace `godot` below if your Godot 4.7 executable has a different name:

```sh
godot --headless --path project --editor --quit
godot --headless --path project --script res://tests/smoke_test.gd
```

The smoke test verifies that `example.barista` loads as `BaristaScript`, retains
its source, and cannot instantiate.

## Run native C++ tests

Tokenizer contracts run as doctest cases inside a test-enabled extension hosted by an
unmodified Godot 4.7 runtime. Select the executable explicitly:

```sh
scons api_version=4.7 target=template_debug barista_tests=yes
python3 tests/test_run_native_suites.py --godot "$(command -v godot)"
python3 tests/run_native_suites.py --godot "$(command -v godot)"
```

For CMake, select its artifact directory:

```sh
cmake -S . -B build/native-cmake -DGODOTCPP_API_VERSION=4.7 -DCMAKE_BUILD_TYPE=Debug -DBARISTA_TESTS=ON
cmake --build build/native-cmake --parallel
python3 tests/run_native_suites.py --godot "$(command -v godot)" --build-dir build/native-cmake
```

Both options default off and require the debug target. Native objects/libraries are isolated
from ordinary distribution builds; each run stages a disposable fixture project. A missing
or wrong library, unknown/empty selection, failed assertion, crash, timeout, or missing
completion record returns nonzero. `--suite tokenizer --case integer_range_is_exact` selects
one exact case. `--list` is informational and never counts as verification. See the
[native test conventions and parity checklist](tests/native/README.md).

All remaining GDScript suites still run against the ordinary debug artifact:

```sh
scons api_version=4.7 target=template_debug barista_tests=no
godot --headless --path project --editor --quit
python3 tests/run_gdscript_suites.py --godot "$(command -v godot)"
python3 tests/verify_native_surface.py --binary-dir project/bin --target-type template_debug
```

CI exercises both native build systems and test-on → test-off → test-on transitions without
clearing caches, checks ordinary debug/release artifacts for test code, and retains every
legacy suite/corpus gate. Add native suites to `tests/native_suites.json`; CI consumes the
whole manifest without filters.

## Layout

- `src/barista_script_language.*` implements language registration metadata.
- `src/barista_script.*` implements the non-executable script resource.
- `src/barista_script_resource_loader.*` loads `.barista` source files.
- `src/register_types.*` owns GDExtension startup and shutdown.
- `project/` is the Godot 4.7 recognition fixture and smoke test.
- `godot-cpp/` is the pinned bindings submodule.

## Verify corpus reproducibility

The `corpus-reproducibility` status check runs once on Linux for each existing CI
workflow event, outside the build matrix. It validates
`scripts/corpus_sources.json`, checks out the public Foundry repository at that
registry's immutable revision, and checks every active importer. The parser is
active; the analyzer is explicitly pending until #45 delivers its importer,
corpus tree, ledger and exact suite invocation. Pending is not a passing corpus.
The job changes no branch-protection settings.

The wrapper and importers use only Python's standard library and Git. Offline
CI configuration tests additionally use the pinned PyYAML parser. CI provisions
Python with `actions/setup-python` before installing it in both jobs, including
every build-matrix platform. For local externally managed Python installations
(such as Homebrew), create and activate a virtual environment first:

```sh
python3 -m venv .venv
. .venv/bin/activate
```

Then install the audit dependency and run the checks:

```sh
python3 -m pip install -r tests/requirements.txt
python3 tests/test_corpus_reproducibility.py
python3 tests/test_corpus_baseline.py
python3 tests/validate_ci.py
```

The default reproducibility tests need no network or credentials. They include
real parser generation from a small set of byte-faithful upstream fixtures;
full pinned-source integration tests are explicitly skipped without `--foundry`.
To run the full checks, use a local Foundry checkout at the revision in
`scripts/corpus_sources.json`, with its `origin` pointing to
`cafecito-games/Foundry` on GitHub and both registered parser/analyzer source
roots present and clean:

```sh
python3 scripts/check_corpus_reproducibility.py --foundry /path/to/Foundry
python3 tests/test_corpus_reproducibility.py --foundry /path/to/Foundry
```

CI supplies this checkout at `.upstream-foundry` using the wrapper's validated
`--github-output "$GITHUB_OUTPUT"` outputs for repository, revision and sparse
paths. It does not build or execute Foundry. The full tests mutate disposable
copies and exercise the real parser importer. Hidden-source mutation fixtures
borrow the supplied checkout's available Git objects read-only and copy only the
registered materialized roots, so shallow sparse partial checkouts need no
additional object fetch. Fixture Git commands isolate global/system settings
and disable transports. `--check` regenerates in temporary
directories and never repairs committed files. Explicit parser regeneration
without `--check` remains available to repair its generated tree. An unrelated
`.foundry/autoload_index_cache.cfg` outside the consumed roots is left alone.
Checks reject symlinks and special filesystem entries without following them.
Consumed upstream bytes are compared directly with the pinned Git blob IDs, so
`assume-unchanged` or `skip-worktree` index hints cannot hide drift; the check
does not change the checkout or its index flags. Ordinary sparse checkouts with
both complete registered roots remain supported.

To deliver another corpus, register its allowlisted local importer and roots,
transition its registry state and baseline `imported` boolean together, and pin
its exact runner invocation. CI derives the importer list and Foundry revision
from the registry; no additional YAML importer command or revision is needed.

Analyzer corpus discovery and exact-case reproduction are documented in
[the checkpoint A guide](docs/analyzer-discovery.md). The final analyzer corpus
remains pending until its complete included population passes.
