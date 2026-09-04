#!/usr/bin/env python3
# Copyright (c) 2026-present Cafecito Games LLC.
# This file is part of BaristaScript, a Godot GDExtension.
# SPDX-License-Identifier: MIT
"""Mechanically port Foundry analyzer/type sources into BaristaScript.

Reads modules/foundry_script sources at the pinned SHA and writes renamed,
seam-routed BaristaScript sources under src/. Intentional D1 deletions and
adapter rewrites are applied after the mechanical rename.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PIN = "c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6"
LICENSE = """\
/**************************************************************************/
/*  {filename:<69}*/
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/{upstream}` @            */
/*  {pin}. FS* -> BS*; engine contact through bs_platform.h / adapters.   */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/
"""

# Upstream basename -> output basename (without extension handling for splits).
PORT_FILES = [
	"fs_type.h",
	"fs_type.cpp",
	"fs_trait_utils.h",
	"fs_trait_utils.cpp",
	"fs_tagged_union.h",
	"fs_builtin_types.h",
	"fs_builtin_types.cpp",
	"fs_builtin_sources.h",
	"fs_builtin_sources.cpp",
	"fs_conformance_registry.h",
	"fs_conformance_registry.cpp",
	"fs_autoload_index.h",
	"fs_autoload_index.cpp",
	"fs_utility_functions.h",
	"fs_utility_functions.cpp",
	"fs_utility_callable.h",
	"fs_utility_callable.cpp",
	"fs_script_extensible_native_hooks.h",
	"fs_script_extensible_native_hooks.cpp",
	"fs_analyzer.h",
	"fs_analyzer.cpp",
	"fs_analyzer_call_validation.cpp",
	"fs_analyzer_conformance.cpp",
	"fs_analyzer_finalization.cpp",
	"fs_analyzer_flow_finality.cpp",
	"fs_analyzer_surface.cpp",
]

GODOT_LICENSE_RE = re.compile(
	r"/\*{10,}\n/\*[^*]*\*/\n(?:/\*[^*]*\*/\n)*\*{10,}/\n+",
	re.MULTILINE,
)

CORE_INCLUDE_RE = re.compile(
	r'^#include\s+"(core/|servers/|scene/|editor/|modules/)[^"]+"\s*$',
	re.MULTILINE,
)

FOUNDRY_INCLUDE_RE = re.compile(
	r'^#include\s+"(foundry_script|fs_[^"]+|fs_numeric_ops\.h)"\s*$',
	re.MULTILINE,
)


def strip_godot_license(text: str) -> str:
	return GODOT_LICENSE_RE.sub("", text, count=1)


def rename_symbols(text: str) -> str:
	replacements = [
		("FOUNDRY_SCRIPT_", "BARISTA_SCRIPT_"),
		("FoundryScript", "BaristaScript"),
		("FoundryTypeInfo", "BSTypeInfo"),
		("FSLanguage", "BaristaScriptLanguage"),
		("FSAnalyzer", "BSAnalyzer"),
		("FSParser", "BSParser"),
		("FSTokenizer", "BSTokenizer"),
		("FSWarning", "BSWarning"),
		("FSCache", "BSCache"),
		("FSParserRef", "BSParserRef"),
		("FSParseCache", "BSParseCache"),
		("FSConformanceRegistry", "BSConformanceRegistry"),
		("FSAutoloadIndex", "BSAutoloadIndex"),
		("FSBuiltinTypes", "BSBuiltinTypes"),
		("FSBuiltinSources", "BSBuiltinSources"),
		("FSTypeCompatibility", "BSTypeCompatibility"),
		("FSNumericConversion", "BSNumericConversion"),
		("FSFunctionState", "BSFunctionState"),
		("FSFunction", "BSFunction"),
		("FSUtilityFunctions", "BSUtilityFunctions"),
		("FSUtilityCallable", "BSUtilityCallable"),
		("FSScriptExtensibleNativeHooks", "BSScriptExtensibleNativeHooks"),
		("FSTaggedUnion", "BSTaggedUnion"),
		("FSTests", "BSTests"),
		("FSWeakContainerType", "BSWeakContainerType"),
		("FSDataType", "BSDataType"),
		("FSRuntimeSpecializationEvidence", "BSRuntimeSpecializationEvidence"),
		("fs_class_or_trait_diagnostic_name", "bs_class_or_trait_diagnostic_name"),
		("fs_diagnostic_type_name_for_path", "bs_diagnostic_type_name_for_path"),
		("fs_diagnostic_file_reference", "bs_diagnostic_file_reference"),
		("fs_numeric_ops.h", "bs_platform.h"),
		("foundry://builtin/", "barista://builtin/"),
		("res://.foundry/builtin/", "res://.barista/builtin/"),
		(".fsb", ".bsb"),
		('".fs"', '".barista"'),
		("foundry_script.h", "barista_script.h"),
		("fs_analyzer.h", "bs_analyzer.h"),
		("fs_type.h", "bs_type.h"),
		("fs_trait_utils.h", "bs_trait_utils.h"),
		("fs_tagged_union.h", "bs_tagged_union.h"),
		("fs_builtin_types.h", "bs_builtin_types.h"),
		("fs_builtin_sources.h", "bs_builtin_sources.h"),
		("fs_conformance_registry.h", "bs_conformance_registry.h"),
		("fs_autoload_index.h", "bs_autoload_index.h"),
		("fs_utility_functions.h", "bs_utility_functions.h"),
		("fs_utility_callable.h", "bs_utility_callable.h"),
		("fs_script_extensible_native_hooks.h", "bs_script_extensible_native_hooks.h"),
		("fs_diagnostic_names.h", "bs_diagnostic_names.h"),
		("fs_cache.h", "bs_cache.h"),
		("fs_parser.h", "bs_parser.h"),
		("fs_function.h", "bs_function.h"),
		("fs_warning.h", "bs_warning.h"),
	]
	for old, new in replacements:
		text = text.replace(old, new)
	# Remaining fs_ identifiers used as free functions / macros.
	text = re.sub(r"\bfs_", "bs_", text)
	text = re.sub(r"\bFS([A-Z])", r"BS\1", text)
	return text


def rewrite_includes(text: str) -> str:
	# Drop engine-module includes; bs_platform.h / adapters cover them.
	text = CORE_INCLUDE_RE.sub("", text)
	# Ensure platform seam after pragma / first include block.
	if '#include "bs_platform.h"' not in text and "#include <" not in text[:500]:
		# Insert after #pragma once if present.
		if "#pragma once" in text:
			text = text.replace("#pragma once", '#pragma once\n\n#include "bs_platform.h"', 1)
		elif '#include "bs_analyzer.h"' in text:
			text = text.replace('#include "bs_analyzer.h"', '#include "bs_analyzer.h"\n#include "bs_platform.h"', 1)
		elif '#include "bs_type.h"' in text:
			text = text.replace('#include "bs_type.h"', '#include "bs_type.h"\n#include "bs_platform.h"', 1)
	# Adapter includes for ScriptServer / CoreConstants / TypeInfo.
	if "ScriptServer::" in text and "bs_script_server.h" not in text:
		text = text.replace('#include "bs_platform.h"', '#include "bs_platform.h"\n#include "bs_script_server.h"', 1)
	if "CoreConstants::" in text and "bs_core_constants.h" not in text:
		text = text.replace('#include "bs_platform.h"', '#include "bs_platform.h"\n#include "bs_core_constants.h"', 1)
	if "BSTypeInfo::" in text and "bs_type_info.h" not in text:
		text = text.replace('#include "bs_platform.h"', '#include "bs_platform.h"\n#include "bs_type_info.h"', 1)
	if "BSNativeDB::" in text and "bs_native_db.h" not in text:
		text = text.replace('#include "bs_platform.h"', '#include "bs_platform.h"\n#include "bs_native_db.h"', 1)
	# Collapse blank runs from removed includes.
	text = re.sub(r"\n{3,}", "\n\n", text)
	return text


def apply_d1_and_adapters(text: str, filename: str) -> str:
	# D1: NumericType field usages become inert / deleted.
	# RecordedTypeArgument.numeric_type field — drop the member.
	text = re.sub(
		r"^\s*NumericType numeric_type = NumericType::NONE;\s*\n",
		"\t\t// D1: NumericType deleted; Variant carrier is the whole numeric type.\n",
		text,
		flags=re.MULTILINE,
	)
	text = re.sub(
		r"^\s*bool numeric_type_is_carrier_erased = false;\s*\n",
		"",
		text,
		flags=re.MULTILINE,
	)
	text = re.sub(
		r"^\s*bool numeric_type_is_explicit = false;\s*\n",
		"",
		text,
		flags=re.MULTILINE,
	)

	# MethodInfo::get_argument_meta — godot-cpp MethodInfo has fields, not this method.
	# Rewrite call sites to the D1-none helper before other ClassDB rewrites.
	text = re.sub(
		r"static_cast<BSTypeInfo::Metadata>\(\s*([^)]+)\.get_argument_meta\(([^)]*)\)\s*\)",
		r"bs_method_argument_meta(\1, \2)",
		text,
	)
	text = re.sub(
		r"\(BSTypeInfo::Metadata\)([^)]+)\.get_argument_meta\(([^)]*)\)",
		r"bs_method_argument_meta(\1, \2)",
		text,
	)
	text = re.sub(
		r"([A-Za-z_][A-Za-z0-9_\.]*)\.get_argument_meta\(",
		r"bs_method_argument_meta(\1, ",
		text,
	)

	# MethodBind::get_argument_meta → always NONE via helper (D1 / no engine MethodBind meta).
	text = text.replace(
		"p_method_bind->get_argument_meta(p_argument)",
		"bs_method_bind_argument_meta(p_method_bind, p_argument)",
	)

	# ClassDB engine-internal shapes → BSNativeDB adapters where godot-cpp differs.
	for old, new in [
		("ClassDB::get_method_info(", "BSNativeDB::get_method_info("),
		("ClassDB::get_signal(", "BSNativeDB::get_signal("),
		("ClassDB::get_property_getter(", "BSNativeDB::get_property_getter("),
		("ClassDB::get_property_setter(", "BSNativeDB::get_property_setter("),
		("ClassDB::get_method(", "BSNativeDB::get_method("),
	]:
		text = text.replace(old, new)

	# ResourceLoader::load static → singleton in godot-cpp.
	text = re.sub(
		r"ResourceLoader::load\(",
		"BSResourceLoader::load(",
		text,
	)

	# Engine:: singleton access
	text = text.replace("Engine::get_singleton()", "Engine::get_singleton()")

	# TESTS_ENABLED blocks for Foundry unit tests — keep structure but rename.
	# Drop includes of deleted numeric ops.
	text = text.replace('#include "bs_platform.h"\n#include "bs_platform.h"', '#include "bs_platform.h"')

	# foundry.fs extension leftovers in string literals for builtin sources.
	if "bs_builtin_sources" in filename:
		text = text.replace("foundry://", "barista://")

	return text


def wrap_namespace(text: str, is_header: bool) -> str:
	if "namespace barista_script" in text:
		return text
	# Insert after includes: find last #include / #ifdef include guard content start.
	lines = text.splitlines(keepends=True)
	insert_at = 0
	seen_code = False
	for i, line in enumerate(lines):
		stripped = line.strip()
		if stripped.startswith("#include") or stripped.startswith("#pragma") or stripped.startswith("#ifdef") or stripped.startswith("#ifndef") or stripped.startswith("#endif") or stripped.startswith("#define") and "BARISTA" in stripped:
			insert_at = i + 1
			continue
		if stripped == "" and not seen_code:
			insert_at = i + 1
			continue
		if stripped.startswith("/*") or stripped.startswith("*") or stripped.startswith("//"):
			continue
		seen_code = True
		break
	# Simpler: after first blank line following includes.
	insert_at = 0
	for i, line in enumerate(lines):
		if line.startswith("#include") or line.startswith("#pragma"):
			insert_at = i + 1
	while insert_at < len(lines) and lines[insert_at].strip() == "":
		insert_at += 1
	lines.insert(insert_at, "\nnamespace barista_script {\n\n")
	if not text.rstrip().endswith("} // namespace barista_script"):
		if not text.endswith("\n"):
			lines.append("\n")
		lines.append("\n} // namespace barista_script\n")
	return "".join(lines)


def transform(upstream_text: str, upstream_name: str, out_name: str) -> str:
	text = strip_godot_license(upstream_text)
	text = rename_symbols(text)
	text = rewrite_includes(text)
	text = apply_d1_and_adapters(text, out_name)
	# Ensure analyzer.cpp keeps bootstrap_root as heap String (existing seam).
	if out_name == "bs_analyzer.cpp":
		text = text.replace(
			"static thread_local String bootstrap_allowed_dependency_root;",
			"// BaristaScript: heap String; TLS godot::String before GDExtension init is unsafe.\n"
			"// Storage lives in bootstrap_root_storage() below / existing seam.",
		)
	header = LICENSE.format(filename=out_name, upstream=upstream_name, pin=PIN)
	body = text.lstrip("\n")
	combined = header + "\n" + body
	combined = wrap_namespace(combined, out_name.endswith(".h"))
	return combined


def main() -> int:
	parser = argparse.ArgumentParser()
	parser.add_argument("--foundry", required=True, type=Path)
	parser.add_argument("--out", required=True, type=Path)
	parser.add_argument("--only", nargs="*", default=None)
	args = parser.parse_args()
	src_root = args.foundry / "modules" / "foundry_script"
	if not src_root.is_dir():
		print(f"missing {src_root}", file=sys.stderr)
		return 1
	args.out.mkdir(parents=True, exist_ok=True)
	selected = set(args.only) if args.only else None
	for name in PORT_FILES:
		if selected is not None and name not in selected and name.replace("fs_", "bs_") not in selected:
			continue
		upstream = src_root / name
		if not upstream.exists():
			print(f"skip missing {upstream}", file=sys.stderr)
			continue
		out_name = name.replace("fs_", "bs_")
		out_path = args.out / out_name
		transformed = transform(upstream.read_text(encoding="utf-8"), name, out_name)
		out_path.write_text(transformed, encoding="utf-8")
		print(f"wrote {out_path} ({len(transformed.splitlines())} lines)")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
