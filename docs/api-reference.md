# API reference contributors

`doc_classes/*.xml` is the authored source for both Godot's in-editor class help
and the static API reference. Edit descriptions and examples there, following
[Godot's XML class-reference format](https://contributing.godotengine.org/en/latest/documentation/class_reference.html).
The supported classes are `BaristaScript`, `BaristaScriptLanguage`, and
`BaristaScriptResourceLoader`. Their script bindings in `src/` define the API;
public C++ helpers, private probes, caches, and native test runners are excluded.
Language/loader integration hooks are inherited from Godot, not new bound methods.

## Build and validate the reference

Use Python 3.11 or newer. No Godot executable, engine compilation, C++ compiler,
or initialized `godot-cpp` submodule is required for a documentation-only build.
For an externally managed Python installation, first create a virtual environment:

```sh
python3 -m venv build/docs-venv
. build/docs-venv/bin/activate
python3 -m pip install -r docs/requirements.txt
python3 scripts/build_reference.py --check
python3 scripts/build_reference.py --output build/docs
python3 tests/test_build_reference.py
```

Open `build/docs/index.html` to inspect all three class pages. `--check` performs
the complete HTML build in temporary storage and never writes authored XML.
`--output` publishes the successfully generated site and can be repeated. It
preserves existing files, so use a dedicated generated directory; stale extra
class pages fail validation. Generated output and tooling caches live under
ignored `build/` and must not be committed.

The first run downloads the official Godot 4.7 source archive identified by the
immutable revision and SHA-256 in `docs/reference-toolchain.json`. Every run
verifies the cached archive before extracting its documentation converter and
engine XML into temporary storage. The converter is Godot's own
[`doc/tools/make_rst.py`](https://github.com/godotengine/godot/blob/5b4e0cb0fd279832bbdd69fed5354d4e5ad26f88/doc/tools/make_rst.py);
we do not maintain a second XML renderer. `docs/requirements.txt` pins Sphinx
and its transitive dependencies. To update tooling, deliberately update both
pins, rerun the regression tests, and inspect the generated pages.

Malformed XML, unresolved class/member references, unsupported classes, missing
pages, and broken local links or fragments fail with nonzero status and a source
filename/context. Engine references resolve only against the pinned engine XML
and link to the Godot 4.7 online reference. External websites are not crawled;
local reference checks do not depend on their availability. The regression suite
mutates disposable copies to verify failures and compares two generated copies
for stable page content. `--source /path/to/doc_classes` supports such fixtures
without editing the authored files.

The **API Reference** CI workflow runs these checks and uploads the
`baristascript-api-reference` HTML artifact. It does not deploy a hosted website.
The existing design and grammar Markdown remains narrative documentation; link
to the reference rather than duplicating method/property descriptions there.

## Verify the embedded editor help

Both existing build hooks consume the same XML files for `editor` and
`template_debug` targets. Release builds omit help data. After changing XML,
verify both build systems and the release path:

```sh
git submodule update --init --recursive
scons api_version=4.7 target=template_debug
cmake -S . -B build/docs-cmake -DGODOTCPP_API_VERSION=4.7 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/docs-cmake --parallel
scons api_version=4.7 target=template_release
python3 scripts/add_license_header.py --check
python3 tests/validate_ci.py
godot --headless --path project --editor --quit
python3 tests/run_gdscript_suites.py --godot "$(command -v godot)"
```

Start the Godot editor with the fixture project and use **Search Help** to open
each supported class. Check the descriptions, examples, `BaristaScript.is_valid`,
and the links to inherited Godot members. This is extension class help;
documentation for declarations within `.barista` source is a separate,
unimplemented language capability. Runtime/placeholder execution is also absent;
static validation success does not imply an executable script.
