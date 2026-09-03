#!/usr/bin/env python3
# audit_platform_seam.py
#
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT

"""Audit `src/bs_platform.h` against the godot-cpp header set.

The manifest at `src/bs_platform_manifest.json` is the demand side: every `core/*` header the
ported Foundry frontend depends on, with the resolution BaristaScript chose for it. godot-cpp is
the supply side. This script checks one against the other and prints the resulting table.

It resolves nothing on its own. An entry the manifest does not explain, a header godot-cpp does not
have, a shim the seam does not define, a deleted symbol the seam does not poison -- each is a
non-zero exit, so a godot-cpp bump or a careless edit fails loudly instead of drifting.

Usage:
    python3 tests/audit_platform_seam.py
    python3 tests/audit_platform_seam.py --manifest OTHER.json
    python3 tests/audit_platform_seam.py --print-index
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# One definition per resolution. A manifest entry carries exactly one of these, and every consumer
# below either handles the value or refuses the manifest -- there is no fall-through.
#
#   mapped       godot-cpp has an exact equivalent. The seam includes it, unwrapped and unrenamed.
#   vendored     No equivalent exists, so the Godot MIT source is copied under `src/thirdparty/`
#                with a provenance comment. The file must be present in the repository.
#   shimmed      No equivalent exists, or a symbol of the header has none, so the seam defines the
#                missing symbol itself. Named symbols must appear in the seam.
#   guarded-out  The seam deliberately supplies nothing and the port drops the include. Valid only
#                with an `omission_reason` saying why that is safe.
#   deleted-D1   The dependency died with the numeric tower. The seam must poison the named symbols
#                so a surviving reference is a compile error rather than a stub.
RESOLUTIONS = ("mapped", "vendored", "shimmed", "guarded-out", "deleted-D1")

REQUIRED_FIELD = {
    "mapped": "godot_cpp_headers",
    "vendored": "vendored_path",
    "shimmed": "shim_symbols",
    "guarded-out": "omission_reason",
    "deleted-D1": "forbidden_symbols",
}

# Fields a resolution may never carry, so a wrong claim is rejected rather than ignored.
FORBIDDEN_FIELDS = {
    "mapped": ("vendored_path", "shim_symbols", "forbidden_symbols", "omission_reason"),
    "vendored": ("godot_cpp_headers", "shim_symbols", "forbidden_symbols", "omission_reason"),
    "shimmed": ("vendored_path", "forbidden_symbols", "omission_reason"),
    "guarded-out": ("godot_cpp_headers", "vendored_path", "shim_symbols", "forbidden_symbols"),
    "deleted-D1": ("godot_cpp_headers", "vendored_path", "shim_symbols", "omission_reason"),
}

OMISSION_REASONS = ("debug-only", "tools-only", "unreferenced")

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', re.MULTILINE)
FIXTURE_SECTION_PATTERN = re.compile(r"^=== \S+/(\S+) ===$")
FIXTURE_INCLUDE_PATTERN = re.compile(r'^(\d+):\s*#\s*include\s+"([^"]+)"$')

# Includes of the port set's own headers. They are upstream file names, not engine dependencies, so
# the seam has nothing to say about them.
PORT_INTERNAL_PREFIXES = ("fs_", "foundry_script.")
API_VERSION_PATTERN = re.compile(r'"api_version"\]\s*=\s*"([0-9]+\.[0-9]+)"')


class AuditError(Exception):
    """A failure that stops the audit before any table can be printed."""


def load_manifest(path):
    if not path.is_file():
        raise AuditError("no manifest at {}".format(path))
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise AuditError("{} is not valid JSON: {}".format(path, error))
    if not isinstance(document, dict):
        raise AuditError("{} is not valid JSON object: top level is {}".format(path, type(document).__name__))
    for field in ("entries", "required_macros", "upstream", "seam_header"):
        if field not in document:
            raise AuditError("{} has no {!r} field".format(path, field))
    if not isinstance(document["entries"], list):
        raise AuditError("{}: 'entries' must be a list, not {}".format(path, type(document["entries"]).__name__))
    if not isinstance(document["required_macros"], list):
        raise AuditError(
            "{}: 'required_macros' must be a list, not {}".format(path, type(document["required_macros"]).__name__)
        )
    if not isinstance(document["upstream"], dict):
        raise AuditError(
            "{}: 'upstream' must be an object, not {}".format(path, type(document["upstream"]).__name__)
        )
    return document


def read_api_version(sconstruct):
    """The build's API version is declared once, in SConstruct; never guess a default."""
    if not sconstruct.is_file():
        raise AuditError("no SConstruct at {}, so the godot-cpp API version is unknown".format(sconstruct))
    match = API_VERSION_PATTERN.search(sconstruct.read_text(encoding="utf-8"))
    if not match:
        raise AuditError("{} does not declare api_version".format(sconstruct))
    return match.group(1)


def godot_cpp_header_index(godot_cpp, api_version, build_profile):
    """Every godot-cpp header the build will offer, checked-in and generated alike.

    The generated half is not guessed: `build_profile.generate_trimmed_api` and
    `binding_generator._get_file_list` are the same functions SCons calls, so the predicted set is
    what the build actually writes, build-profile trimming included. When a generated tree is
    already on disk it is used as well, and a prediction it does not contain is reported as
    generator drift rather than quietly ignored.
    """
    include_root = godot_cpp / "include" / "godot_cpp"
    if not include_root.is_dir():
        raise AuditError(
            "no godot-cpp headers at {}; run 'git submodule update --init --recursive'".format(include_root)
        )
    api_path = godot_cpp / "gdextension" / "extension_api-{}.json".format(api_version.replace(".", "-"))
    if not api_path.is_file():
        raise AuditError("no godot-cpp API description at {}".format(api_path))

    checked_in = {
        "godot_cpp/" + path.relative_to(include_root).as_posix()
        for path in include_root.rglob("*")
        if path.is_file()
    }

    sys.path.insert(0, str(godot_cpp))
    try:
        from binding_generator import _get_file_list
        from build_profile import generate_trimmed_api
    except ImportError as error:
        raise AuditError("cannot import godot-cpp's binding generator from {}: {}".format(godot_cpp, error))
    finally:
        sys.path.pop(0)

    profile = str(build_profile) if build_profile.is_file() else ""
    api = generate_trimmed_api(str(api_path), profile)
    prefix = (godot_cpp / "gen" / "include").as_posix() + "/"
    predicted = set()
    for entry in _get_file_list(api, str(godot_cpp), headers=True, sources=False):
        if entry.startswith(prefix) and entry.startswith(prefix + "godot_cpp/"):
            predicted.add(entry[len(prefix) :])

    generated_root = godot_cpp / "gen" / "include" / "godot_cpp"
    drift = []
    if generated_root.is_dir():
        on_disk = {
            "godot_cpp/" + path.relative_to(generated_root).as_posix()
            for path in generated_root.rglob("*")
            if path.is_file()
        }
        drift = sorted(predicted - on_disk)
        predicted |= on_disk

    return checked_in | predicted, drift


def load_site_fixture(path):
    """The verbatim capture of the upstream include preambles, as {"file:line": header}.

    The manifest's `sites` are claims about someone else's source tree, which this repository cannot
    reach at audit time. The fixture is that source tree's own output, checked in, so the claims can
    be verified instead of trusted.
    """
    if not path.is_file():
        raise AuditError("no upstream site fixture at {}".format(path))
    sites = {}
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        section = FIXTURE_SECTION_PATTERN.match(line)
        if section:
            current = section.group(1)
            continue
        include = FIXTURE_INCLUDE_PATTERN.match(line)
        if include and current is not None:
            sites["{}:{}".format(current, include.group(1))] = include.group(2)
    if not sites:
        raise AuditError("{} names no include sites; it is not a usable capture".format(path))
    return sites


def check_site_coverage(sites, claimed, failures):
    """Every upstream dependency in the capture is accounted for by some manifest entry."""
    for site in sorted(sites):
        header = sites[site]
        if header.startswith(PORT_INTERNAL_PREFIXES):
            continue
        if site not in claimed:
            failures.append("{} includes {} at {}, which no manifest entry explains".format(
                site.rsplit(":", 1)[0], header, site
            ))


def seam_includes(seam):
    if not seam.is_file():
        raise AuditError("no seam header at {}".format(seam))
    text = seam.read_text(encoding="utf-8")
    return set(INCLUDE_PATTERN.findall(text)), text


def defines_symbol(seam_text, symbol):
    """True when the seam defines `symbol` as a macro, a class, or a type alias."""
    patterns = (
        r"^\s*#\s*define\s+{}\b".format(re.escape(symbol)),
        r"^\s*(?:class|struct)\s+{}\b".format(re.escape(symbol)),
        r"^\s*using\s+{}\s*=".format(re.escape(symbol)),
        r"^\s*typedef\s+.*\b{}\s*;".format(re.escape(symbol)),
    )
    return any(re.search(pattern, seam_text, re.MULTILINE) for pattern in patterns)


def poisons_symbol(seam_text, symbol):
    """True when the seam redirects `symbol` at something that does not exist."""
    return re.search(r"^\s*#\s*define\s+{}\s+\S+".format(re.escape(symbol)), seam_text, re.MULTILINE) is not None


def as_string_list(value):
    """The strings in `value`, or nothing at all when it is not a list of strings.

    A malformed field has already been reported by the time this runs; this exists so the digest and
    the table survive it rather than crashing on a type they did not expect.
    """
    if not isinstance(value, list):
        return []
    return [item for item in value if isinstance(item, str)]


def string_list(entry, field, failures, core_header):
    value = entry.get(field)
    if not isinstance(value, list) or not value or not all(isinstance(item, str) for item in value):
        failures.append("{}: {!r} must be a non-empty list of strings".format(core_header, field))
        return []
    return value


def check_entry(entry, index, seam_headers, seam_text, port_set, sites, claimed, failures):
    core_header = entry.get("core_header")
    if not isinstance(core_header, str) or not core_header:
        failures.append("an entry has no 'core_header'")
        return None

    resolution = entry.get("resolution")
    if resolution is None:
        failures.append("{}: no 'resolution'; every entry states one of {}".format(core_header, ", ".join(RESOLUTIONS)))
        return None
    if resolution not in RESOLUTIONS:
        failures.append(
            "{}: unknown resolution {!r}; the vocabulary is {}".format(core_header, resolution, ", ".join(RESOLUTIONS))
        )
        return None

    for field in FORBIDDEN_FIELDS[resolution]:
        if field in entry:
            failures.append("{}: a {!r} entry may not carry {!r}".format(core_header, resolution, field))
    if REQUIRED_FIELD[resolution] not in entry:
        failures.append("{}: a {!r} entry needs {!r}".format(core_header, resolution, REQUIRED_FIELD[resolution]))
        return None

    for site in string_list(entry, "sites", failures, core_header):
        source = site.rsplit(":", 1)[0]
        if source not in port_set:
            failures.append("{}: site {!r} is outside the declared port set".format(core_header, site))
            continue
        if site not in sites:
            failures.append("{}: the upstream capture has no include at {}".format(core_header, site))
        elif sites[site] != core_header:
            failures.append(
                "{}: the upstream capture has {} at {}, not {}".format(core_header, sites[site], site, core_header)
            )
        else:
            claimed.add(site)

    detail = ""
    if resolution == "mapped":
        headers = string_list(entry, "godot_cpp_headers", failures, core_header)
        for header in headers:
            if header not in index:
                failures.append("{}: godot-cpp has no {}".format(core_header, header))
            if header not in seam_headers:
                failures.append("{}: the seam does not include {}".format(core_header, header))
        detail = ", ".join(headers)
    elif resolution == "vendored":
        vendored = entry.get("vendored_path")
        if not isinstance(vendored, str) or not vendored.startswith("src/thirdparty/"):
            failures.append("{}: 'vendored_path' must be a path under src/thirdparty/".format(core_header))
        elif not (ROOT / vendored).is_file():
            failures.append("{}: nothing vendored at {}".format(core_header, vendored))
        detail = vendored if isinstance(vendored, str) else ""
    elif resolution == "shimmed":
        symbols = string_list(entry, "shim_symbols", failures, core_header)
        for symbol in symbols:
            if not defines_symbol(seam_text, symbol):
                failures.append("{}: the seam does not define the shim {}".format(core_header, symbol))
        for header in as_string_list(entry.get("godot_cpp_headers")):
            if header not in index:
                failures.append("{}: godot-cpp has no {}".format(core_header, header))
            if header not in seam_headers:
                failures.append("{}: the seam does not include {}".format(core_header, header))
        detail = ", ".join(symbols)
    elif resolution == "guarded-out":
        reason = entry.get("omission_reason")
        if reason not in OMISSION_REASONS:
            failures.append(
                "{}: unknown omission_reason {!r}; expected one of {}".format(
                    core_header, reason, ", ".join(OMISSION_REASONS)
                )
            )
        detail = reason if isinstance(reason, str) else ""
    elif resolution == "deleted-D1":
        symbols = string_list(entry, "forbidden_symbols", failures, core_header)
        for symbol in symbols:
            if not poisons_symbol(seam_text, symbol):
                failures.append("{}: the seam does not poison {}".format(core_header, symbol))
        detail = ", ".join(symbols)
    else:  # pragma: no cover - RESOLUTIONS and the branches above are checked in step.
        failures.append("{}: resolution {!r} has no handler".format(core_header, resolution))

    return {
        "core_header": core_header,
        "resolution": resolution,
        "detail": detail,
        "godot_cpp_headers": sorted(as_string_list(entry.get("godot_cpp_headers"))),
        "symbols": sorted(as_string_list(entry.get("shim_symbols")) + as_string_list(entry.get("forbidden_symbols"))),
        "vendored_path": entry.get("vendored_path") if isinstance(entry.get("vendored_path"), str) else "",
        "omission_reason": entry.get("omission_reason") if isinstance(entry.get("omission_reason"), str) else "",
    }


def semantic_digest(rows, required_macros):
    """A digest of what the audit decided -- never of where the files live or when it ran."""
    payload = {
        "resolutions": [
            {
                "core_header": row["core_header"],
                "resolution": row["resolution"],
                "godot_cpp_headers": row["godot_cpp_headers"],
                "symbols": row["symbols"],
                "vendored_path": row["vendored_path"],
                "omission_reason": row["omission_reason"],
            }
            for row in rows
        ],
        "required_macros": sorted(required_macros),
    }
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def check_required_macros(macros, godot_cpp, seam_text, failures):
    error_macros = godot_cpp / "include" / "godot_cpp" / "core" / "error_macros.hpp"
    if not error_macros.is_file():
        failures.append("godot-cpp has no {}".format(error_macros))
        return
    text = error_macros.read_text(encoding="utf-8")
    for macro in macros:
        if not isinstance(macro, str):
            failures.append("'required_macros' holds a {}, not a macro name".format(type(macro).__name__))
    for macro in sorted(as_string_list(macros)):
        if not re.search(r"^\s*#\s*define\s+{}\b".format(re.escape(macro)), text, re.MULTILINE):
            failures.append("godot-cpp no longer defines {}".format(macro))
        if macro not in seam_text:
            failures.append("the seam does not assert that {} is defined".format(macro))


def cmake_library_sources(text):
    """The arguments of every `target_sources(${LIBNAME} ...)` call, or None when there are none.

    Searching the file as one string would accept a path that appears only in a comment, in an
    unrelated variable, or on another target, so the call is parsed: comments are stripped, the
    argument list is read to its matching parenthesis, and only blocks naming the extension target
    count.
    """
    stripped = re.sub(r"#[^\n]*", "", text)
    sources = None
    for match in re.finditer(r"\btarget_sources\s*\(", stripped):
        depth = 1
        index = match.end()
        while index < len(stripped) and depth:
            if stripped[index] == "(":
                depth += 1
            elif stripped[index] == ")":
                depth -= 1
            index += 1
        arguments = stripped[match.end() : index - 1].split()
        if not arguments or arguments[0] != "${LIBNAME}":
            continue
        sources = (sources or set()) | set(arguments[1:])
    return sources


def check_builds_compile_the_proof_sources(manifest, cmakelists, failures):
    """Both supported builds have to compile the proof sources, not just one of them.

    SCons globs `src/*.cpp`, so a proof source anywhere else is silently dropped from that build.
    CMake lists its sources explicitly, so one missing from `target_sources` is silently dropped
    from that one. Either way the seam would stop being proven while the audit still passed.
    """
    sources = as_string_list(manifest.get("seam_proof_sources"))
    if not cmakelists.is_file():
        failures.append("no CMakeLists.txt at {}".format(cmakelists))
        return
    listed = cmake_library_sources(cmakelists.read_text(encoding="utf-8"))
    if listed is None:
        failures.append("{} has no target_sources(${{LIBNAME}} ...) block".format(cmakelists))
        return
    for source in sources:
        path = Path(source)
        if path.parent.as_posix() != "src" or path.suffix != ".cpp":
            failures.append("{} is not matched by SConstruct's src/*.cpp glob".format(source))
        if source not in listed:
            failures.append("{} is not listed in CMakeLists.txt's target_sources".format(source))


def check_shims_are_compiled(manifest, rows, failures):
    """A shim nothing compiles is a shim that rots.

    Including the seam header proves its mappings resolve; it does not compile a macro that is never
    expanded or a class whose members are never called. The manifest names the translation units
    that do, and every shim symbol has to appear in one of them.
    """
    sources = as_string_list(manifest.get("seam_proof_sources"))
    if not sources:
        failures.append("the manifest names no 'seam_proof_sources'")
        return
    text = ""
    for source in sources:
        path = ROOT / source
        if not path.is_file():
            failures.append("no seam proof source at {}".format(source))
            continue
        text += path.read_text(encoding="utf-8")
    for row in rows:
        if row["resolution"] != "shimmed":
            continue
        for symbol in row["symbols"]:
            if not re.search(r"\b{}\b".format(re.escape(symbol)), text):
                failures.append(
                    "{}: no seam proof source uses the shim {}".format(row["core_header"], symbol)
                )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--manifest", type=Path, default=ROOT / "src" / "bs_platform_manifest.json")
    parser.add_argument("--seam", type=Path, default=None)
    parser.add_argument("--godot-cpp", type=Path, default=ROOT / "godot-cpp")
    parser.add_argument("--build-profile", type=Path, default=ROOT / "build_profile.json")
    parser.add_argument("--sconstruct", type=Path, default=ROOT / "SConstruct")
    parser.add_argument("--cmakelists", type=Path, default=ROOT / "CMakeLists.txt")
    parser.add_argument("--api-version", default=None)
    parser.add_argument("--print-index", action="store_true", help="print the godot-cpp header set and exit")
    arguments = parser.parse_args(argv)

    try:
        api_version = arguments.api_version or read_api_version(arguments.sconstruct)
        index, drift = godot_cpp_header_index(arguments.godot_cpp, api_version, arguments.build_profile)
        if arguments.print_index:
            print("\n".join(sorted(index)))
            return 0
        manifest = load_manifest(arguments.manifest)
        seam = arguments.seam or ROOT / manifest["seam_header"]
        seam_headers, seam_text = seam_includes(seam)
        fixture = manifest["upstream"].get("site_fixture")
        if not isinstance(fixture, str) or not fixture:
            raise AuditError("{} declares no 'site_fixture'".format(arguments.manifest))
        sites = load_site_fixture(ROOT / fixture)
    except AuditError as error:
        print("audit failed: {}".format(error), file=sys.stderr)
        return 1

    failures = []
    for header in sorted(drift):
        failures.append("godot-cpp generator drift: {} is predicted but not generated".format(header))

    port_set = set(as_string_list(manifest["upstream"].get("port_set")))
    if not port_set:
        failures.append("the manifest declares no upstream port set")

    rows = []
    seen = set()
    claimed = set()
    for entry in manifest["entries"]:
        if not isinstance(entry, dict):
            failures.append("an entry is {}, not an object".format(type(entry).__name__))
            continue
        core_header = entry.get("core_header")
        if core_header in seen:
            failures.append("{} is listed twice".format(core_header))
            continue
        if isinstance(core_header, str):
            seen.add(core_header)
        row = check_entry(entry, index, seam_headers, seam_text, port_set, sites, claimed, failures)
        if row is not None:
            rows.append(row)

    check_site_coverage(sites, claimed, failures)
    check_required_macros(manifest["required_macros"], arguments.godot_cpp, seam_text, failures)
    check_shims_are_compiled(manifest, rows, failures)
    check_builds_compile_the_proof_sources(manifest, arguments.cmakelists, failures)

    allowed = set(as_string_list(manifest.get("seam_support_headers")))
    for entry in manifest["entries"]:
        if isinstance(entry, dict):
            allowed.update(as_string_list(entry.get("godot_cpp_headers")))
    for header in sorted(seam_headers - allowed):
        failures.append("the seam includes {}, which no manifest entry explains".format(header))

    rows.sort(key=lambda row: (row["core_header"],))
    width = max([len(row["core_header"]) for row in rows] + [len("upstream header")])
    print("{}  {:<11}  {}".format("upstream header".ljust(width), "resolution", "resolves to"))
    print("{}  {}  {}".format("-" * width, "-" * 11, "-" * 40))
    for row in rows:
        print("{}  {:<11}  {}".format(row["core_header"].ljust(width), row["resolution"], row["detail"]))
    print("")
    print("entries {}".format(len(rows)))
    print("digest  {}".format(semantic_digest(rows, manifest["required_macros"])))

    if failures:
        print("")
        for failure in failures:
            print("FAIL: {}".format(failure), file=sys.stderr)
        print("{} platform seam failure(s)".format(len(failures)), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
