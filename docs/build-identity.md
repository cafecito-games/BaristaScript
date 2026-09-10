# Build identity and diagnostics

[build_versions.json](../build_versions.json) selects the extension version, Godot API and
runtime, precision, and toolchain versions, including explicit platform overrides. SCons,
CMake, CI and the checks consume this configuration. Inspect the selected values with:

```sh
python3 scripts/build_config.py
python3 scripts/build_config.py --get godot_runtime
python3 scripts/build_config.py --get godot_api
python3 scripts/build_config.py --platform web
```

Recommended build commands use these defaults; explicit conflicting API or precision choices
fail. The formatter requirements file is a checked projection of this configuration. After
deliberately changing the formatter selection, regenerate it and install the selected version:

```sh
python3 scripts/build_config.py --format format-requirement > scripts/requirements-format.txt
python3 -m pip install -r scripts/requirements-format.txt
python3 scripts/check_format.py
```

Use a virtual environment when the system Python is externally managed. This projection is
committed with the configuration change; generated build outputs remain ignored.

## Inspect the library in use

`BaristaScript.get_build_info()` is a no-argument, read-only instance method available in
ordinary debug and release libraries. It returns a fresh `Dictionary` from the compiled
library's metadata. The authored [BaristaScript XML](../doc_classes/BaristaScript.xml) owns
the API description and generated reference. From a host language such as GDScript:

```gdscript
var script := BaristaScript.new()
print(JSON.stringify(script.get_build_info()))
print(Engine.get_version_info())
```

This creates a resource, not an executable `.barista` script instance. Static validation
and script execution capabilities are unchanged. Mutating the returned dictionary cannot
change the library's identity or subsequent calls.

For a complete diagnostic report, explicitly select both the actual library and the engine:

```sh
python3 scripts/query_build_info.py --godot /path/to/godot --library /path/to/library
```

The report includes the loaded build identity, the library's SHA-256, the actual host engine
version, and separate current-checkout information. The selected `godot_runtime` is a build
configuration value; it is not a measurement of the engine that loaded the library. Likewise,
compiled source identity does not change when the current checkout changes. A stale or copied
library remains diagnosable without requiring it to match the checkout.

For verification that also requires the loaded identity to match the current inputs, add
`--require-current`. This strict check requires clean, known source and dependency identities;
revision plus a dirty/unknown state cannot distinguish different uncommitted or archive contents.
For an artifact that cannot run on this host, inspect its embedded
identity without loading Godot:

```sh
python3 scripts/query_build_info.py --godot /path/to/godot --library /path/to/library --require-current
python3 scripts/build_metadata.py --artifact /path/to/library
```

Include the ordinary diagnostic report when filing a bug. If loading fails, include that
error, artifact-only inspection output, and the actual host's `godot --version` output.
Keep any reported mismatch visible rather than substituting the current checkout's values.

## Identity fields

Schema `1` is defined and validated in [scripts/build_metadata.py](../scripts/build_metadata.py):

| Field | Meaning |
| --- | --- |
| `schema` | Metadata schema version. |
| `extension_version` | Selected extension version. |
| `config_sha256` | SHA-256 of the canonical validated configuration, distinct from the library's file hash. |
| `godot_api` | Selected extension API family. |
| `godot_runtime` | Selected runtime version; query the actual host separately. |
| `source.revision`, `source.state` | Extension source commit and state observed when building. |
| `godot_cpp.selected_revision` | Submodule revision selected in the extension's Git index. |
| `godot_cpp.revision`, `godot_cpp.state` | Actual bindings checkout revision and state observed when building. |
| `build.platform`, `build.architecture` | Selected platform and architecture, including a combined universal artifact when applicable. |
| `build.target`, `build.precision`, `build.native_tests` | Selected target, precision and boolean native-test enablement. |

States are `clean`, `dirty`, or `unknown`. Staged/tracked changes, nonignored untracked files,
and changed submodule state can make an identity dirty; ignored build/import outputs do not.
A dirty revision identifies the base commit, not every modified byte. Source archives without
Git metadata honestly report unknown revision/state; they do not inherit a containing
checkout's identity. The selected and actual bindings revisions are kept separately so drift
remains visible. These are build-time observations, not claims about the current filesystem.
