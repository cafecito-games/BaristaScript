# BaristaScript

BaristaScript is an experimental scripting language for Godot. This repository
currently provides the first integration milestone: Godot 4.7 recognizes
`.barista` files as non-executable `Script` resources.

The project is based on the official
[`godot-cpp-template`](https://github.com/godotengine/godot-cpp-template) and
uses `godot-cpp` 10.0.0-rc2 with the Godot 4.7 extension API.

## Current capabilities

- Registers `BaristaScript` with Godot's scripting-language registry.
- Recognizes and loads `.barista` source files.
- Preserves source text in a `BaristaScript` resource.
- Opens a clean path for a parser, runtime, and editor tooling.

Parsing, validation, execution, script instances, completion, debugging, and
profiling are intentionally not implemented yet.

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

## Layout

- `src/barista_script_language.*` implements language registration metadata.
- `src/barista_script.*` implements the non-executable script resource.
- `src/barista_script_resource_loader.*` loads `.barista` source files.
- `src/register_types.*` owns GDExtension startup and shutdown.
- `project/` is the Godot 4.7 recognition fixture and smoke test.
- `godot-cpp/` is the pinned bindings submodule.
