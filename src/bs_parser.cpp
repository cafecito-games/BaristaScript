/**************************************************************************/
/*  bs_parser.cpp                                                         */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_parser.h"

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_cache.h"
#include "bs_tokenizer_buffer.h"

/**
 * Hard fork of Foundry's parser (`modules/foundry_script/fs_parser.cpp` @
 * c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6). See `bs_parser.h` for the summary of what diverges;
 * each divergence is also marked where it occurs, with the upstream `file:line` it replaces.
 */

namespace barista_script {

namespace {
// The default-answering host, and the installed one. Neither is ever null: an unset host is this
// instance, so no call site has to branch on its absence.
BSParserHost default_parser_host;
BSParserHost *installed_parser_host = &default_parser_host;
} // namespace

BSParserHost *BSParserHost::get_singleton() {
	return installed_parser_host;
}

BSParserHost *BSParserHost::set_singleton(BSParserHost *p_host) {
	BSParserHost *previous = installed_parser_host;
	installed_parser_host = (p_host == nullptr) ? &default_parser_host : p_host;
	return previous;
}

#ifdef DEBUG_ENABLED
bool BSParser::is_project_ignoring_warnings = false;
BSWarning::WarnLevel BSParser::warning_levels[BSWarning::WARNING_MAX];
LocalVector<BSParser::WarningDirectoryRule> BSParser::warning_directory_rules;
#endif // DEBUG_ENABLED

#ifdef TOOLS_ENABLED
HashMap<String, String> BSParser::theme_color_names;
#endif // TOOLS_ENABLED

HashMap<StringName, BSParser::AnnotationInfo> BSParser::valid_annotations;

namespace {
// `varray` is a core helper (`core/variant/array.h`) that builds an `Array` from its arguments;
// godot-cpp has no counterpart. It is not a platform-seam mapping: no file in the declared port set
// includes the header it comes from -- upstream reaches it transitively through `core/variant/variant.h`
// -- so there is no upstream include site for the seam to answer. The one consumer is the annotation
// registration table below, which spells each annotation's default arguments with it.
template <typename... Args>
Vector<Variant> varray(const Args &...p_args) {
	Vector<Variant> array;
	(array.push_back(Variant(p_args)), ...);
	return array;
}

// Increments a depth counter on construction and decrements it on destruction so
// every return path out of a recursive parse function stays balanced.
struct RecursionDepthGuard {
	int &depth;
	explicit RecursionDepthGuard(int &p_depth) :
			depth(p_depth) { depth++; }
	~RecursionDepthGuard() { depth--; }
};
} // namespace

// The native base class the engine's global-class table records for `p_class_name`, or an empty
// String when the name is not a global class. Upstream reaches the same table through the
// engine-internal `ScriptServer` (fs_parser.cpp:7566-7567 @ c9d5e35); an extension reaches it
// through `ProjectSettings`, which is where the engine persists it.
static String _global_class_native_base(const String &p_class_name) {
	const ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return String();
	}
	const TypedArray<Dictionary> global_classes = const_cast<ProjectSettings *>(settings)->get_global_class_list();
	for (int i = 0; i < global_classes.size(); i++) {
		const Dictionary entry = global_classes[i];
		if (String(entry.get("class", String())) == p_class_name) {
			return entry.get("base", String());
		}
	}
	return String();
}

void BSParser::cleanup() {
	clear_builtin_type_cache();
	valid_annotations.clear();
}

void BSParser::get_annotation_list(List<MethodInfo> *r_annotations) const {
	for (const KeyValue<StringName, AnnotationInfo> &E : valid_annotations) {
		r_annotations->push_back(E.value.info);
	}
}

bool BSParser::annotation_exists(const String &p_annotation_name) const {
	return valid_annotations.has(p_annotation_name);
}

#ifdef DEBUG_ENABLED
void BSParser::update_project_settings() {
	// Upstream reads these through core's `GLOBAL_GET` macro (fs_parser.cpp:94,103 @ c9d5e35). The
	// seam records that godot-cpp has no such macro and that ported code spells the override-aware
	// read out instead (src/bs_platform_manifest.json, core/config/project_settings.h).
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return;
	}
	is_project_ignoring_warnings = !settings->get_setting_with_override("debug/barista_script/warnings/enable").booleanize();

	for (int i = 0; i < BSWarning::WARNING_MAX; i++) {
		const String setting_path = BSWarning::get_setting_path_from_code((BSWarning::Code)i);
		warning_levels[i] = (BSWarning::WarnLevel)(int)settings->get_setting_with_override(setting_path);
	}

	warning_directory_rules.clear();

	const Dictionary rules = settings->get_setting_with_override("debug/barista_script/warnings/directory_rules");
	// godot-cpp's `Dictionary` is engine-backed and offers no iterator, so the keys are taken first.
	const Array rule_directories = rules.keys();
	for (int rule_index = 0; rule_index < rule_directories.size(); rule_index++) {
		const Variant rule_key = rule_directories[rule_index];
		String dir = String(rule_key).simplify_path();
		ERR_CONTINUE_MSG(!dir.begins_with("res://"), R"(Paths in the project setting "debug/barista_script/warnings/directory_rules" keys must start with the "res://" prefix.)");
		if (!dir.ends_with("/")) {
			dir += '/';
		}

		const int decision = rules[rule_key];
		ERR_CONTINUE(decision < 0 || decision >= WarningDirectoryRule::DECISION_MAX);

		warning_directory_rules.push_back({ dir, (WarningDirectoryRule::Decision)decision });
	}

	struct RuleSort {
		bool operator()(const WarningDirectoryRule &p_a, const WarningDirectoryRule &p_b) const {
			return p_a.directory_path.count("/") > p_b.directory_path.count("/");
		}
	};

	warning_directory_rules.sort_custom<RuleSort>();
}

// D-deferred: upstream's `invalidate_analysis_on_strict_settings_change()` (fs_parser.cpp:127-153
// @ c9d5e35) drops already-built parsers and scripts when a strict-analysis project setting flips.
// Both halves it needs -- the analyzer that reads those settings and the cache's parse-tree store
// (`BSCache::invalidate_analysis()`, absent by the deliberate scoping in bs_cache.h:29-31) -- arrive
// with M3, and a version that dropped nothing would be a stub that silently kept stale analysis.
#endif // DEBUG_ENABLED

BSParser::BSParser() {
	// Register valid annotations.
	if (unlikely(valid_annotations.is_empty())) {
		// Script annotations.
		register_annotation(MethodInfo("@tool"), AnnotationInfo::SCRIPT, &BSParser::tool_annotation);
		register_annotation(MethodInfo("@icon", PropertyInfo(Variant::STRING, "icon_path")), AnnotationInfo::SCRIPT, &BSParser::icon_annotation);
		register_annotation(MethodInfo("@static_unload"), AnnotationInfo::SCRIPT, &BSParser::static_unload_annotation);
		register_annotation(
				MethodInfo("@autoload", PropertyInfo(Variant::ARRAY, "depends_on"), PropertyInfo(Variant::INT, "order_id")),
				AnnotationInfo::SCRIPT,
				&BSParser::autoload_annotation,
				varray(Array(), 0));
		register_annotation(MethodInfo("@keep_name"),
				AnnotationInfo::CLASS | AnnotationInfo::VARIABLE | AnnotationInfo::FUNCTION |
						AnnotationInfo::SIGNAL | AnnotationInfo::CONSTANT,
				&BSParser::keep_name_annotation);
		register_annotation(MethodInfo("@noreturn"), AnnotationInfo::FUNCTION, &BSParser::noreturn_annotation);
		// Onready annotation.
		register_annotation(MethodInfo("@onready"), AnnotationInfo::VARIABLE, &BSParser::onready_annotation);
		// Export annotations.
		register_annotation(MethodInfo("@export"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_NONE, Variant::NIL>);
		register_annotation(MethodInfo("@export_enum", PropertyInfo(Variant::STRING, "names")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_ENUM, Variant::NIL>, varray(), true);
		register_annotation(MethodInfo("@export_file", PropertyInfo(Variant::STRING, "filter")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_FILE, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_file_path", PropertyInfo(Variant::STRING, "filter")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_FILE_PATH, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_dir"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_DIR, Variant::STRING>);
		register_annotation(MethodInfo("@export_global_file", PropertyInfo(Variant::STRING, "filter")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_GLOBAL_FILE, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_global_dir"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_GLOBAL_DIR, Variant::STRING>);
		register_annotation(MethodInfo("@export_multiline", PropertyInfo(Variant::STRING, "hint")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_MULTILINE_TEXT, Variant::STRING>, varray(""), true);
		register_annotation(MethodInfo("@export_placeholder", PropertyInfo(Variant::STRING, "placeholder")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_PLACEHOLDER_TEXT, Variant::STRING>);
		register_annotation(MethodInfo("@export_range", PropertyInfo(Variant::FLOAT, "min"), PropertyInfo(Variant::FLOAT, "max"), PropertyInfo(Variant::FLOAT, "step"), PropertyInfo(Variant::STRING, "extra_hints")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_RANGE, Variant::FLOAT>, varray(1.0, ""), true);
		register_annotation(MethodInfo("@export_exp_easing", PropertyInfo(Variant::STRING, "hints")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_EXP_EASING, Variant::FLOAT>, varray(""), true);
		register_annotation(MethodInfo("@export_color_no_alpha"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_COLOR_NO_ALPHA, Variant::COLOR>);
		register_annotation(MethodInfo("@export_node_path", PropertyInfo(Variant::STRING, "type")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_NODE_PATH_VALID_TYPES, Variant::NODE_PATH>, varray(""), true);
		register_annotation(MethodInfo("@export_flags", PropertyInfo(Variant::STRING, "names")), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_FLAGS, Variant::INT>, varray(), true);
		register_annotation(MethodInfo("@export_flags_2d_render"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_2D_RENDER, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_2d_physics"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_2D_PHYSICS, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_2d_navigation"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_2D_NAVIGATION, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_3d_render"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_3D_RENDER, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_3d_physics"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_3D_PHYSICS, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_3d_navigation"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_3D_NAVIGATION, Variant::INT>);
		register_annotation(MethodInfo("@export_flags_avoidance"), AnnotationInfo::VARIABLE, &BSParser::export_annotations<PROPERTY_HINT_LAYERS_AVOIDANCE, Variant::INT>);
		register_annotation(MethodInfo("@export_storage"), AnnotationInfo::VARIABLE, &BSParser::export_storage_annotation);
		register_annotation(MethodInfo("@export_custom", PropertyInfo(Variant::INT, "hint", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_ENUM, "PropertyHint"), PropertyInfo(Variant::STRING, "hint_string"), PropertyInfo(Variant::INT, "usage", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_CLASS_IS_BITFIELD, "PropertyUsageFlags")), AnnotationInfo::VARIABLE, &BSParser::export_custom_annotation, varray(PROPERTY_USAGE_DEFAULT));
		register_annotation(MethodInfo("@export_tool_button", PropertyInfo(Variant::STRING, "text"), PropertyInfo(Variant::STRING, "icon")), AnnotationInfo::VARIABLE, &BSParser::export_tool_button_annotation, varray(""));
		// Export grouping annotations.
		register_annotation(MethodInfo("@export_category", PropertyInfo(Variant::STRING, "name")), AnnotationInfo::STANDALONE, &BSParser::export_group_annotations<PROPERTY_USAGE_CATEGORY>);
		register_annotation(MethodInfo("@export_group", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::STRING, "prefix")), AnnotationInfo::STANDALONE, &BSParser::export_group_annotations<PROPERTY_USAGE_GROUP>, varray(""));
		register_annotation(MethodInfo("@export_subgroup", PropertyInfo(Variant::STRING, "name"), PropertyInfo(Variant::STRING, "prefix")), AnnotationInfo::STANDALONE, &BSParser::export_group_annotations<PROPERTY_USAGE_SUBGROUP>, varray(""));
		// Warning annotations.
		register_annotation(MethodInfo("@warning_ignore", PropertyInfo(Variant::STRING, "warning")), AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STATEMENT, &BSParser::warning_ignore_annotation, varray(), true);
		register_annotation(MethodInfo("@warning_ignore_start", PropertyInfo(Variant::STRING, "warning")), AnnotationInfo::STANDALONE, &BSParser::warning_ignore_region_annotations, varray(), true);
		register_annotation(MethodInfo("@warning_ignore_restore", PropertyInfo(Variant::STRING, "warning")), AnnotationInfo::STANDALONE, &BSParser::warning_ignore_region_annotations, varray(), true);
		// Networking.
		// Keep in sync with `rpc_annotation()` and `SceneRPCInterface::_parse_rpc_config()`.
		register_annotation(MethodInfo("@rpc", PropertyInfo(Variant::STRING, "mode"), PropertyInfo(Variant::STRING, "sync"), PropertyInfo(Variant::STRING, "transfer_mode"), PropertyInfo(Variant::INT, "transfer_channel")), AnnotationInfo::FUNCTION, &BSParser::rpc_annotation, varray("authority", "call_remote", "reliable", 0));
	}

#ifdef DEBUG_ENABLED
	for (int i = 0; i < BSWarning::WARNING_MAX; i++) {
		warning_ignore_start_lines[i] = INT_MAX;
	}
#endif // DEBUG_ENABLED

#ifdef TOOLS_ENABLED
	if (unlikely(theme_color_names.is_empty())) {
		// Vectors.
		theme_color_names.insert("x", "axis_x_color");
		theme_color_names.insert("y", "axis_y_color");
		theme_color_names.insert("z", "axis_z_color");
		theme_color_names.insert("w", "axis_w_color");

		// Color.
		theme_color_names.insert("r", "axis_x_color");
		theme_color_names.insert("r8", "axis_x_color");
		theme_color_names.insert("g", "axis_y_color");
		theme_color_names.insert("g8", "axis_y_color");
		theme_color_names.insert("b", "axis_z_color");
		theme_color_names.insert("b8", "axis_z_color");
		theme_color_names.insert("a", "axis_w_color");
		theme_color_names.insert("a8", "axis_w_color");
	}
#endif // TOOLS_ENABLED
}

BSParser::~BSParser() {
	while (list != nullptr) {
		Node *element = list;
		list = list->next;
		memdelete(element);
	}
}

void BSParser::clear() {
	BSParser tmp;
	tmp = *this;
	*this = BSParser();
}

void BSParser::push_error(const String &p_message, const Node *p_origin) {
	// TODO: Improve error reporting by pointing at source code.
	// TODO: Errors might point at more than one place at once (e.g. show previous declaration).
	panic_mode = true;

	// A diagnostic the parser derives while still standing on the token that follows a tokenizer
	// rejection is a second, differently worded complaint about the token the tokenizer already
	// rejected -- which the fail-closed contract forbids. `func f() -> uint:` would otherwise report
	// both `"uint" is reserved ...` and `Expected return type or "void" after "->"`, and
	// `var x = 1L` both the suffix rejection and `Expected expression for variable initial value`.
	//
	// The window is exactly right, not approximately: `current_follows_tokenizer_error` is set only
	// when `advance()` reached `current` by skipping `ERROR` tokens, and it is cleared by the next
	// `advance()`. Everything the parser says inside that window is about the hole the tokenizer
	// left behind. A node-anchored diagnostic is exempt: it is about a node somewhere else in the
	// tree, not about the token under the cursor.
	if (current_follows_tokenizer_error && p_origin == nullptr) {
		return;
	}

	// TODO: Improve positional information.
	if (p_origin == nullptr) {
		errors.push_back({ p_message, previous.start_line, previous.start_column, previous.end_line, previous.end_column });
	} else {
		errors.push_back({ p_message, p_origin->start_line, p_origin->start_column, p_origin->end_line, p_origin->end_column });
	}
}

void BSParser::push_error_at(const String &p_message, const BSTokenizer::Token &p_token) {
	panic_mode = true;
	errors.push_back({ p_message, p_token.start_line, p_token.start_column, p_token.end_line, p_token.end_column });
}

void BSParser::push_tokenizer_error(const BSTokenizer::Token &p_error_token) {
	tokenizer_failed = true;
	push_error_at(p_error_token.literal, p_error_token);
}

Vector<const BSParser::ParserError *> BSParser::get_errors_in_source_order() const {
	// The emission index is the final tiebreaker, so two diagnostics at the same position keep the
	// order they were reported in.
	struct OrderedError {
		const ParserError *error = nullptr;
		int index = 0;
		bool operator<(const OrderedError &p_other) const {
			if (error->line != p_other.error->line) {
				return error->line < p_other.error->line;
			}
			if (error->column != p_other.error->column) {
				return error->column < p_other.error->column;
			}
			return index < p_other.index;
		}
	};

	LocalVector<OrderedError> ordered;
	ordered.resize(errors.size());
	int index = 0;
	for (const ParserError &error : errors) {
		ordered[index] = { &error, index };
		index++;
	}
	ordered.sort();

	Vector<const ParserError *> result;
	result.resize(ordered.size());
	for (uint32_t i = 0; i < ordered.size(); i++) {
		result.write[i] = ordered[i].error;
	}
	return result;
}

#ifdef DEBUG_ENABLED
void BSParser::push_warning(const Node *p_source, BSWarning::Code p_code, const Vector<String> &p_symbols) {
	ERR_FAIL_NULL(p_source);
	ERR_FAIL_INDEX(p_code, BSWarning::WARNING_MAX);

	if (is_project_ignoring_warnings || is_script_ignoring_warnings) {
		return;
	}

	const BSWarning::WarnLevel warn_level = warning_levels[p_code];
	if (warn_level == BSWarning::IGNORE) {
		return;
	}

	PendingWarning pw;
	pw.source = p_source;
	pw.code = p_code;
	pw.treated_as_error = warn_level == BSWarning::ERROR;
	pw.symbols = p_symbols;

	pending_warnings.push_back(pw);
}

void BSParser::apply_pending_warnings() {
	for (const PendingWarning &pw : pending_warnings) {
		if (warning_ignored_lines[pw.code].has(pw.source->start_line)) {
			continue;
		}
		if (warning_ignore_start_lines[pw.code] <= pw.source->start_line) {
			continue;
		}

		BSWarning warning;
		warning.code = pw.code;
		warning.symbols = pw.symbols;
		warning.start_line = pw.source->start_line;
		warning.start_column = pw.source->start_column;
		warning.end_line = pw.source->end_line;
		warning.end_column = pw.source->end_column;

		if (pw.treated_as_error) {
			push_error(warning.get_message() + String(" (Warning treated as error.)"), pw.source);
			continue;
		}

		List<BSWarning>::Element *before = nullptr;
		for (List<BSWarning>::Element *E = warnings.front(); E; E = E->next()) {
			if (E->get().start_line > warning.start_line) {
				break;
			}
			before = E;
		}
		if (before) {
			warnings.insert_after(before, warning);
		} else {
			warnings.push_front(warning);
		}
	}

	pending_warnings.clear();
}

void BSParser::evaluate_warning_directory_rules_for_script_path() {
	is_script_ignoring_warnings = false;
	for (const WarningDirectoryRule &rule : warning_directory_rules) {
		if (script_path.begins_with(rule.directory_path)) {
			switch (rule.decision) {
				case WarningDirectoryRule::DECISION_EXCLUDE:
					is_script_ignoring_warnings = true;
					return; // Stop checking rules.
				case WarningDirectoryRule::DECISION_INCLUDE:
					is_script_ignoring_warnings = false;
					return; // Stop checking rules.
				case WarningDirectoryRule::DECISION_MAX:
					return; // Unreachable.
			}
		}
	}
}
#endif // DEBUG_ENABLED

void BSParser::override_completion_context(const Node *p_for_node, CompletionType p_type, Node *p_node, int p_argument) {
	if (!for_completion) {
		return;
	}
	if (p_for_node == nullptr || completion_context.node != p_for_node) {
		return;
	}
	CompletionContext context;
	context.type = p_type;
	context.current_class = current_class;
	context.current_function = current_function;
	context.current_suite = current_suite;
	context.current_line = tokenizer->get_cursor_line();
	context.current_argument = p_argument;
	context.node = p_node;
	context.parser = this;
	if (!completion_call_stack.is_empty()) {
		context.call = completion_call_stack.back()->get();
	}
	completion_context = context;
}

void BSParser::make_completion_context(CompletionType p_type, Node *p_node, int p_argument, bool p_force, const Vector<IdentifierNode *> *p_chain) {
	if (!for_completion || (!p_force && completion_context.type != COMPLETION_NONE)) {
		return;
	}
	if (previous.cursor_place != BSTokenizerText::CURSOR_MIDDLE && previous.cursor_place != BSTokenizerText::CURSOR_END && current.cursor_place == BSTokenizerText::CURSOR_NONE) {
		return;
	}
	CompletionContext context;
	context.type = p_type;
	context.current_class = current_class;
	context.current_function = current_function;
	context.current_suite = current_suite;
	context.current_line = tokenizer->get_cursor_line();
	context.current_argument = p_argument;
	context.node = p_node;
	context.parser = this;
	if (p_chain != nullptr) {
		context.chain = *p_chain;
	}
	if (!completion_call_stack.is_empty()) {
		context.call = completion_call_stack.back()->get();
	}
	completion_context = context;
}

void BSParser::make_completion_context(CompletionType p_type, Variant::Type p_builtin_type, bool p_force) {
	if (!for_completion || (!p_force && completion_context.type != COMPLETION_NONE)) {
		return;
	}
	if (previous.cursor_place != BSTokenizerText::CURSOR_MIDDLE && previous.cursor_place != BSTokenizerText::CURSOR_END && current.cursor_place == BSTokenizerText::CURSOR_NONE) {
		return;
	}
	CompletionContext context;
	context.type = p_type;
	context.current_class = current_class;
	context.current_function = current_function;
	context.current_suite = current_suite;
	context.current_line = tokenizer->get_cursor_line();
	context.builtin_type = p_builtin_type;
	context.parser = this;
	if (!completion_call_stack.is_empty()) {
		context.call = completion_call_stack.back()->get();
	}
	completion_context = context;
}

void BSParser::push_completion_call(Node *p_call) {
	if (!for_completion) {
		return;
	}
	CompletionCall call;
	call.call = p_call;
	call.argument = 0;
	completion_call_stack.push_back(call);
}

void BSParser::pop_completion_call() {
	if (!for_completion) {
		return;
	}
	ERR_FAIL_COND_MSG(completion_call_stack.is_empty(), "Trying to pop empty completion call stack");
	completion_call_stack.pop_back();
}

void BSParser::set_last_completion_call_arg(int p_argument) {
	if (!for_completion) {
		return;
	}
	ERR_FAIL_COND_MSG(completion_call_stack.is_empty(), "Trying to set argument on empty completion call stack");
	completion_call_stack.back()->get().argument = p_argument;
}

Error BSParser::parse(const String &p_source_code, const String &p_script_path, bool p_for_completion, bool p_parse_body) {
	clear();

	String source = p_source_code;
	int cursor_line = -1;
	int cursor_column = -1;
	for_completion = p_for_completion;
	parse_body = p_parse_body;

	// Upstream reads the editor's configured indent width here (fs_parser.cpp:468-470 @ c9d5e35).
	// `editor/settings/editor_settings.h` is guarded out of the seam as tools-only
	// (src/bs_platform_manifest.json), so the tokenizer keeps the four-space default every
	// non-editor host already uses; only column numbers inside a tab-indented line differ.
	const int tab_size = 4;

	if (p_for_completion) {
		// Remove cursor sentinel char.
		const PackedStringArray lines = p_source_code.split("\n");
		cursor_line = 1;
		cursor_column = 1;
		for (int i = 0; i < lines.size(); i++) {
			bool found = false;
			const String &line = lines[i];
			for (int j = 0; j < line.length(); j++) {
				if (line[j] == char32_t(0xFFFF)) {
					found = true;
					break;
				} else if (line[j] == '\t') {
					cursor_column += tab_size - 1;
				}
				cursor_column++;
			}
			if (found) {
				break;
			}
			cursor_line++;
			cursor_column = 1;
		}

		// Upstream spells this `replace_first` (fs_parser.cpp:520 @ c9d5e35), which godot-cpp's
		// `String` does not have. The completion sentinel is written exactly once by the caller and
		// the loop above stops at the first one, so replacing every occurrence is the same edit.
		source = source.replace(String::chr(0xFFFF), String());
	}

	BSTokenizerText *text_tokenizer = memnew(BSTokenizerText);
	text_tokenizer->set_source_code(source);

	tokenizer = text_tokenizer;
	tokenizer->set_cursor_position(cursor_line, cursor_column);

	script_path = p_script_path.simplify_path();

#ifdef DEBUG_ENABLED
	evaluate_warning_directory_rules_for_script_path();
#endif // DEBUG_ENABLED

	current = tokenizer->scan();
	// Avoid error or newline as the first token.
	// The latter can mess with the parser when opening files filled exclusively with comments and newlines.
	current_follows_tokenizer_error = false;
	while (current.type == BSTokenizer::Token::ERROR || current.type == BSTokenizer::Token::NEWLINE) {
		if (current.type == BSTokenizer::Token::ERROR) {
			// At the token's own span: there is no `previous` here at all, so upstream's
			// `push_error()` reports the very first diagnostic in a file at 0:0
			// (fs_parser.cpp:547 @ c9d5e35), which is not a position in any source.
			push_tokenizer_error(current);
			current_follows_tokenizer_error = true;
		}
		current = tokenizer->scan();
	}

#ifdef DEBUG_ENABLED
	// Warn about parsing an empty script file:
	if (current.type == BSTokenizer::Token::TK_EOF) {
		// Create a dummy Node for the warning, pointing to the very beginning of the file
		Node *nd = alloc_node<PassNode>();
		nd->start_line = 1;
		nd->start_column = 0;
		nd->end_line = 1;
		push_warning(nd, BSWarning::EMPTY_FILE);
	}
#endif // DEBUG_ENABLED

	push_multiline(false); // Keep one for the whole parsing.
	parse_program();
	pop_multiline();

#ifdef TOOLS_ENABLED
	comment_data = tokenizer->get_comments();
#endif // TOOLS_ENABLED

	memdelete(text_tokenizer);
	tokenizer = nullptr;

#ifdef DEBUG_ENABLED
	if (multiline_stack.size() > 0) {
		ERR_PRINT("Parser bug: Imbalanced multiline stack.");
	}
#endif // DEBUG_ENABLED

	if (errors.is_empty()) {
		return OK;
	} else {
		return ERR_PARSE_ERROR;
	}
}

Error BSParser::parse_binary(const PackedByteArray &p_binary, const String &p_script_path) {
	// `parse()` clears first and this did not (fs_parser.cpp:5750 @ c9d5e35), so a `BSParser` reused
	// across a text parse and a buffer parse -- or across two buffer parses -- carried the first
	// run's diagnostics and nodes into the second, and could reject well-formed input for an error
	// that belonged to source it had already finished with.
	clear();

	BSTokenizerBuffer *buffer_tokenizer = memnew(BSTokenizerBuffer);
	Error err = buffer_tokenizer->set_code_buffer(p_binary);

	if (err) {
		memdelete(buffer_tokenizer);
		return err;
	}

	tokenizer = buffer_tokenizer;

	script_path = p_script_path.simplify_path();

#ifdef DEBUG_ENABLED
	evaluate_warning_directory_rules_for_script_path();
#endif // DEBUG_ENABLED

	current = tokenizer->scan();
	// Avoid error or newline as the first token.
	// The latter can mess with the parser when opening files filled exclusively with comments and newlines.
	current_follows_tokenizer_error = false;
	while (current.type == BSTokenizer::Token::ERROR || current.type == BSTokenizer::Token::NEWLINE) {
		if (current.type == BSTokenizer::Token::ERROR) {
			// At the token's own span: there is no `previous` here at all, so upstream's
			// `push_error()` reports the very first diagnostic in a file at 0:0
			// (fs_parser.cpp:547 @ c9d5e35), which is not a position in any source.
			push_tokenizer_error(current);
			current_follows_tokenizer_error = true;
		}
		current = tokenizer->scan();
	}

	push_multiline(false); // Keep one for the whole parsing.
	parse_program();
	pop_multiline();

	memdelete(buffer_tokenizer);
	tokenizer = nullptr;

	if (errors.is_empty()) {
		return OK;
	} else {
		return ERR_PARSE_ERROR;
	}
}

BSTokenizer::Token BSParser::advance() {
	lambda_ended = false; // Empty marker since we're past the end in any case.

	if (current.type == BSTokenizer::Token::TK_EOF) {
		ERR_FAIL_COND_V_MSG(current.type == BSTokenizer::Token::TK_EOF, current, "BaristaScript parser bug: Trying to advance past the end of stream.");
	}
	previous = current;
	// Cleared on every advance, not only when a token is scanned: the window the flag describes is
	// "the parser has not moved past the token that followed the tokenizer's rejection", and a
	// buffered lookahead moves past it just as a fresh scan does.
	const bool lookahead_followed_tokenizer_error = lookahead_follows_tokenizer_error;
	current_follows_tokenizer_error = false;
	if (has_lookahead) {
		current = lookahead;
		has_lookahead = false;
		current_follows_tokenizer_error = lookahead_followed_tokenizer_error;
	} else {
		current = tokenizer->scan();
		while (current.type == BSTokenizer::Token::ERROR) {
			push_tokenizer_error(current);
			current_follows_tokenizer_error = true;
			current = tokenizer->scan();
		}
	}
	if (previous.type != BSTokenizer::Token::DEDENT) { // `DEDENT` belongs to the next non-empty line.
		for (Node *n : nodes_in_progress) {
			update_extents(n);
		}
	}
	return previous;
}

// Returns the token that follows `current` without consuming `current`. The peeked token is
// buffered so the next `advance()` reuses it. Use sparingly: it scans ahead under the current
// multiline mode, so only peek when the decision is made before that mode can change.
const BSTokenizer::Token &BSParser::peek() {
	if (!has_lookahead) {
		lookahead = tokenizer->scan();
		lookahead_follows_tokenizer_error = false;
		while (lookahead.type == BSTokenizer::Token::ERROR) {
			push_tokenizer_error(lookahead);
			lookahead_follows_tokenizer_error = true;
			lookahead = tokenizer->scan();
		}
		has_lookahead = true;
	}
	return lookahead;
}

bool BSParser::match(BSTokenizer::Token::Type p_token_type) {
	if (!check(p_token_type)) {
		return false;
	}
	advance();
	return true;
}

bool BSParser::check(BSTokenizer::Token::Type p_token_type) const {
	if (p_token_type == BSTokenizer::Token::IDENTIFIER) {
		return current.is_identifier();
	}
	return current.type == p_token_type;
}

bool BSParser::consume(BSTokenizer::Token::Type p_token_type, const String &p_error_message) {
	if (match(p_token_type)) {
		return true;
	}
	push_error(p_error_message);
	return false;
}

String BSParser::get_node_type_name(Node::Type p_type) {
	switch (p_type) {
		case Node::NONE:
			return "NONE";
		case Node::ANNOTATION:
			return "ANNOTATION";
		case Node::ANNOTATION_DECLARATION:
			return "ANNOTATION_DECLARATION";
		case Node::ARRAY:
			return "ARRAY";
		case Node::ASSERT:
			return "ASSERT";
		case Node::ASSIGNMENT:
			return "ASSIGNMENT";
		case Node::AWAIT:
			return "AWAIT";
		case Node::BINARY_OPERATOR:
			return "BINARY_OPERATOR";
		case Node::BREAK:
			return "BREAK";
		case Node::BREAKPOINT:
			return "BREAKPOINT";
		case Node::CALL:
			return "CALL";
		case Node::CAST:
			return "CAST";
		case Node::CLASS:
			return "CLASS";
		case Node::CONFORMANCE:
			return "CONFORMANCE";
		case Node::CONSTANT:
			return "CONSTANT";
		case Node::CONTINUE:
			return "CONTINUE";
		case Node::DICTIONARY:
			return "DICTIONARY";
		case Node::ENUM:
			return "ENUM";
		case Node::FOR:
			return "FOR";
		case Node::FUNCTION:
			return "FUNCTION";
		case Node::GET_NODE:
			return "GET_NODE";
		case Node::IDENTIFIER:
			return "IDENTIFIER";
		case Node::IF:
			return "IF";
		case Node::LAMBDA:
			return "LAMBDA";
		case Node::LITERAL:
			return "LITERAL";
		case Node::MATCH:
			return "MATCH";
		case Node::MATCH_BRANCH:
			return "MATCH_BRANCH";
		case Node::PARAMETER:
			return "PARAMETER";
		case Node::PASS:
			return "PASS";
		case Node::PATTERN:
			return "PATTERN";
		case Node::PRELOAD:
			return "PRELOAD";
		case Node::RETURN:
			return "RETURN";
		case Node::SELF:
			return "SELF";
		case Node::SIGNAL:
			return "SIGNAL";
		case Node::SUBSCRIPT:
			return "SUBSCRIPT";
		case Node::SUITE:
			return "SUITE";
		case Node::TERNARY_OPERATOR:
			return "TERNARY_OPERATOR";
		case Node::TUPLE:
			return "TUPLE";
		case Node::TUPLE_LITERAL:
			return "TUPLE_LITERAL";
		case Node::TYPE:
			return "TYPE";
		case Node::TYPE_ALIAS:
			return "TYPE_ALIAS";
		case Node::TYPE_PARAMETER:
			return "TYPE_PARAMETER";
		case Node::TYPE_TEST:
			return "TYPE_TEST";
		case Node::UNARY_OPERATOR:
			return "UNARY_OPERATOR";
		case Node::VARIABLE:
			return "VARIABLE";
		case Node::VARIABLE_DESTRUCTURE:
			return "VARIABLE_DESTRUCTURE";
		case Node::WHILE:
			return "WHILE";
		case Node::NODE_TYPE_MAX:
			return "NODE_TYPE_MAX";
	}
	// Unreachable: the switch above is exhaustive over `Node::Type` and the build makes an
	// unhandled enumerator an error. A value outside the enumeration is undefined behaviour that
	// already happened, so this names it rather than inventing a node kind.
	return "<invalid node type>";
}

void BSParser::reject_reserved_type_name() {
	// (D1) `uint`, `ulong` and `long` are reserved type names: recognized in type position and always
	// rejected, never silently treated as user identifiers in a type (docs/GRAMMAR.md sections 2.5
	// and 7.1). The tokenizer rejects them in the positions the token stream alone settles -- after
	// `->`, `as`, `is`, and a `:` outside `{` (bs_tokenizer.cpp, `_is_type_position()`) -- and emits
	// `Token::RESERVED_TYPE_NAME` everywhere else, because only a parser knows whether a name stands
	// in a type position. This is where the rest is decided.
	//
	// It reports at the token's own span and does not consume. The caller's own `consume(IDENTIFIER)` then takes the token as
	// it always would (`check(IDENTIFIER)` accepts `RESERVED_TYPE_NAME`, since the spelling is still a
	// legal ordinary name), so the tree keeps the shape it would have had and exactly one diagnostic
	// is produced -- not a rejection here plus a derived "expected a type" from the caller.
	//
	// The message comes from `BSTokenizer::removed_type_name_diagnostic()`, which is its only
	// definition, so the tokenizer's rejection and this one cannot word the same rule differently.
	if (current.type == BSTokenizer::Token::RESERVED_TYPE_NAME) {
		// Reported at the spelling's own span, not at `previous`: the reader has to be pointed at
		// the name, not at the `:` or `[` that happened to precede it.
		push_error_at(BSTokenizer::removed_type_name_diagnostic(current.get_identifier()), current);
	}
}

bool BSParser::is_at_end() const {
	return check(BSTokenizer::Token::TK_EOF);
}

void BSParser::synchronize() {
	panic_mode = false;
	while (!is_at_end()) {
		if (previous.type == BSTokenizer::Token::NEWLINE || previous.type == BSTokenizer::Token::SEMICOLON) {
			return;
		}

		switch (current.type) {
			case BSTokenizer::Token::ABSTRACT:
			case BSTokenizer::Token::FINAL:
			case BSTokenizer::Token::CLASS:
			case BSTokenizer::Token::FUNC:
			case BSTokenizer::Token::STATIC:
			case BSTokenizer::Token::VAR:
			case BSTokenizer::Token::TK_CONST:
			case BSTokenizer::Token::SIGNAL:
			//case BSTokenizer::Token::IF: // Can also be inside expressions.
			case BSTokenizer::Token::FOR:
			case BSTokenizer::Token::WHILE:
			case BSTokenizer::Token::MATCH:
			case BSTokenizer::Token::RETURN:
			case BSTokenizer::Token::ANNOTATION:
				return;
			default:
				// Do nothing.
				break;
		}

		// The `async` modifier is a contextual identifier rather than a keyword token, so it is
		// not covered by the switch above. Treat it as a declaration sync point only when it
		// directly precedes a `static`/`abstract`/`func` declaration, so a recovered function
		// keeps its `is_declared_async` / `is_coroutine` metadata. A stray `async` identifier
		// inside a broken expression is left to ordinary recovery to avoid halting on a false
		// modifier and cascading into worse recovery.
		if (current.type == BSTokenizer::Token::IDENTIFIER && current.get_identifier() == StringName("async")) {
			const BSTokenizer::Token::Type next_type = peek().type;
			if (next_type == BSTokenizer::Token::FUNC ||
					next_type == BSTokenizer::Token::STATIC ||
					next_type == BSTokenizer::Token::ABSTRACT ||
					next_type == BSTokenizer::Token::FINAL) {
				return;
			}
		}

		advance();
	}
}

void BSParser::push_multiline(bool p_state) {
	multiline_stack.push_back(p_state);
	tokenizer->set_multiline_mode(p_state);
	if (p_state) {
		// Consume potential whitespace tokens already waiting in line.
		while (current.type == BSTokenizer::Token::NEWLINE || current.type == BSTokenizer::Token::INDENT || current.type == BSTokenizer::Token::DEDENT) {
			current = tokenizer->scan(); // Don't call advance() here, as we don't want to change the previous token.
		}
	}
}

void BSParser::pop_multiline() {
	ERR_FAIL_COND_MSG(multiline_stack.is_empty(), "Parser bug: trying to pop from multiline stack without available value.");
	multiline_stack.pop_back();
	tokenizer->set_multiline_mode(multiline_stack.size() > 0 ? multiline_stack.back()->get() : false);
}

bool BSParser::is_statement_end_token() const {
	return check(BSTokenizer::Token::NEWLINE) || check(BSTokenizer::Token::SEMICOLON) || check(BSTokenizer::Token::TK_EOF);
}

bool BSParser::is_statement_end() const {
	return lambda_ended || in_lambda || is_statement_end_token();
}

void BSParser::end_statement(const String &p_context) {
	bool found = false;
	while (is_statement_end() && !is_at_end()) {
		// Remove sequential newlines/semicolons.
		if (is_statement_end_token()) {
			// Only consume if this is an actual token.
			advance();
		} else if (lambda_ended) {
			lambda_ended = false; // Consume this "token".
			found = true;
			break;
		} else {
			if (!found) {
				lambda_ended = true; // Mark the lambda as done since we found something else to end the statement.
				found = true;
			}
			break;
		}

		found = true;
	}
	if (!found && !is_at_end()) {
		push_error(vformat(R"(Expected end of statement after %s, found "%s" instead.)", p_context, current.get_name()));
	}
}

void BSParser::parse_program() {
	head = alloc_node<ClassNode>();
	head->start_line = 1;
	head->end_line = 1;
	head->fqcn = BaristaScript::canonicalize_path(script_path);
	current_class = head;

	auto push_pending_annotations_to_head = [&]() {
		if (!annotation_stack.is_empty()) {
			for (AnnotationNode *annot : annotation_stack) {
				head->annotations.push_back(annot);
			}
			annotation_stack.clear();
		}
	};

	bool has_top_level_annotation = false;
	auto parse_top_level_annotations = [&](uint32_t p_valid_targets) {
		while (!check(BSTokenizer::Token::TK_EOF)) {
			if (match(BSTokenizer::Token::ANNOTATION)) {
				has_top_level_annotation = true;
				AnnotationNode *annotation = parse_annotation(p_valid_targets);
				if (annotation != nullptr) {
					if (annotation->applies_to(AnnotationInfo::CLASS)) {
						// We do not know in advance what the annotation will be applied to: the `head` class or the subsequent inner class.
						// If we encounter `class_name`, `extends` or pure `SCRIPT` annotation, then it's `head`, otherwise it's an inner class.
						annotation_stack.push_back(annotation);
					} else if (annotation->applies_to(AnnotationInfo::SCRIPT)) {
						push_pending_annotations_to_head();
						if (annotation->name == SNAME("@tool") || annotation->name == SNAME("@icon") || annotation->name == SNAME("@static_unload")) {
							// Some annotations need to be resolved and applied in the parser.
							// The root class is not in any class, so `head->outer == nullptr`.
							annotation->apply(this, head, nullptr);
						} else {
							head->annotations.push_back(annotation);
						}
					} else if (annotation->applies_to(AnnotationInfo::STANDALONE)) {
						if (previous.type != BSTokenizer::Token::NEWLINE) {
							push_error(R"(Expected newline after a standalone annotation.)");
						}
						if (annotation->name == SNAME("@export_category") || annotation->name == SNAME("@export_group") || annotation->name == SNAME("@export_subgroup")) {
							head->add_member_group(annotation);
							// This annotation must appear after script-level annotations and `class_name`/`extends`,
							// so we stop looking for script-level stuff.
							return false;
						} else if (annotation->name == SNAME("@warning_ignore_start") || annotation->name == SNAME("@warning_ignore_restore")) {
							// Some annotations need to be resolved and applied in the parser.
							annotation->apply(this, nullptr, nullptr);
						} else {
							push_error(R"(Unexpected standalone annotation.)");
						}
					} else {
						annotation_stack.push_back(annotation);
						// This annotation must appear after script-level annotations and `class_name`/`extends`,
						// so we stop looking for script-level stuff.
						return false;
					}
				}
			} else if (check(BSTokenizer::Token::LITERAL) && current.literal.get_type() == Variant::STRING) {
				// Allow strings in class body as multiline comments.
				advance();
				if (!match(BSTokenizer::Token::NEWLINE)) {
					push_error("Expected newline after comment string.");
				}
			} else {
				break;
			}
		}

		return true;
	};

	bool can_have_class_or_extends = parse_top_level_annotations(AnnotationInfo::SCRIPT | AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STANDALONE);

	if (current.type == BSTokenizer::Token::NAMESPACE || current.type == BSTokenizer::Token::IMPORT || current.type == BSTokenizer::Token::CLASS_NAME || current.type == BSTokenizer::Token::TRAIT_NAME || current.type == BSTokenizer::Token::ENUM_NAME || current.type == BSTokenizer::Token::EXTENDS || current.type == BSTokenizer::Token::USES) {
		// Set range of the class to only start at the top-level declaration if present.
		reset_extents(head, current);
	}

	if ((current.type == BSTokenizer::Token::NAMESPACE || current.type == BSTokenizer::Token::IMPORT) && !annotation_stack.is_empty()) {
		bool was_in_panic_mode = panic_mode;
		push_error(R"(Class annotations must appear after "namespace" and "import" declarations.)");
		annotation_stack.clear();
		panic_mode = was_in_panic_mode;
	}

	bool can_have_namespace_or_import = can_have_class_or_extends;
	while (can_have_namespace_or_import) {
		switch (current.type) {
			case BSTokenizer::Token::NAMESPACE:
				advance();
				parse_namespace();
				break;
			case BSTokenizer::Token::IMPORT:
				advance();
				parse_import();
				break;
			case BSTokenizer::Token::LITERAL:
				if (current.literal.get_type() == Variant::STRING) {
					// Allow strings in class body as multiline comments.
					advance();
					if (!match(BSTokenizer::Token::NEWLINE)) {
						push_error("Expected newline after comment string.");
					}
					break;
				}
				[[fallthrough]];
			default:
				can_have_namespace_or_import = false;
				break;
		}

		if (panic_mode) {
			synchronize();
		}
	}

	if (can_have_class_or_extends) {
		can_have_class_or_extends = parse_top_level_annotations(AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STANDALONE);
	}

	while (can_have_class_or_extends) {
		// Order here doesn't matter, but there should be only one of each at most.
		switch (current.type) {
			case BSTokenizer::Token::FINAL: {
				// A top-level `final` marks the whole-file head class final, but only when it
				// immediately precedes the head keyword it applies to: `class_name` or `extends`.
				// Otherwise it is an ordinary declaration modifier on the first body member; leave it
				// unconsumed so `parse_class_body()` validates it through the shared collector.
				const BSTokenizer::Token::Type next_type = peek().type;
				if (next_type == BSTokenizer::Token::TRAIT_NAME) {
					// A trait is meant to be mixed in, so it cannot be final. Reject it here rather
					// than marking the head final, mirroring how inline `final trait` is rejected.
					advance();
					push_error(R"(The "final" modifier cannot be applied to traits.)");
					break;
				}
				if (next_type != BSTokenizer::Token::CLASS_NAME &&
						next_type != BSTokenizer::Token::EXTENDS) {
					can_have_class_or_extends = false;
					break;
				}
				advance();
				if (head->is_final) {
					push_error(R"(The "final" modifier was already specified.)");
				} else if (head->is_abstract) {
					push_error(R"(The "final" and "abstract" modifiers cannot be combined.)");
				} else {
					head->is_final = true;
				}
			} break;
			case BSTokenizer::Token::ABSTRACT: {
				// A top-level `abstract` marks the whole-file head class abstract, but only when it
				// immediately precedes the head keyword it applies to: `class_name`, `trait_name`, or
				// `extends`. A file with neither `class_name` nor `extends` has no head declaration to
				// mark abstract, so it must add an explicit `extends` (for example
				// `abstract extends RefCounted`) to be abstract.
				//
				// When `abstract` is followed by anything else (`var`, `func`, `class`, `trait`, ...)
				// it is an ordinary declaration modifier on the first body member; leave it unconsumed
				// so `parse_class_body()` validates it through the shared modifier collector.
				const BSTokenizer::Token::Type next_type = peek().type;
				if (next_type != BSTokenizer::Token::CLASS_NAME &&
						next_type != BSTokenizer::Token::TRAIT_NAME &&
						next_type != BSTokenizer::Token::EXTENDS) {
					can_have_class_or_extends = false;
					break;
				}
				advance();
				if (head->is_abstract) {
					push_error(R"(The "abstract" modifier was already specified.)");
				} else if (head->is_final) {
					push_error(R"(The "final" and "abstract" modifiers cannot be combined.)");
				} else {
					head->is_abstract = true;
				}
			} break;
			case BSTokenizer::Token::CLASS_NAME:
				push_pending_annotations_to_head();
				advance();
				if (head->is_enum_file) {
					push_error(R"("enum_name" cannot be combined with "class_name" in the same file.)");
				} else if (head->is_tuple_file) {
					push_error(R"("tuple_name" cannot be combined with "class_name" in the same file.)");
				} else if (head->trait_name_used) {
					push_error(R"("class_name" cannot be combined with "trait_name" in the same file.)");
				} else if (head->identifier != nullptr) {
					push_error(R"("class_name" can only be used once.)");
				} else {
					parse_class_name();
				}
				break;
			case BSTokenizer::Token::TRAIT_NAME:
				push_pending_annotations_to_head();
				advance();
				if (head->is_enum_file) {
					push_error(R"("enum_name" cannot be combined with "trait_name" in the same file.)");
				} else if (head->is_tuple_file) {
					push_error(R"("tuple_name" cannot be combined with "trait_name" in the same file.)");
				} else if (head->trait_name_used) {
					push_error(R"("trait_name" can only be used once.)");
				} else if (head->identifier != nullptr) {
					push_error(R"("trait_name" cannot be combined with "class_name" in the same file.)");
				} else {
					parse_trait_name();
				}
				break;
			case BSTokenizer::Token::ENUM_NAME: {
				push_pending_annotations_to_head();
				advance();
				bool can_register_enum_file = true;
				if (has_top_level_annotation) {
					push_error(R"(An "enum_name" file may only contain its enum declaration.)");
					can_register_enum_file = false;
				}
				parse_enum_name(can_register_enum_file);
			} break;
			case BSTokenizer::Token::TUPLE_NAME: {
				push_pending_annotations_to_head();
				advance();
				bool can_register_tuple_file = true;
				if (has_top_level_annotation) {
					push_error(R"(A "tuple_name" file may only contain its tuple declaration.)");
					can_register_tuple_file = false;
				}
				parse_tuple_name(can_register_tuple_file);
			} break;
			case BSTokenizer::Token::EXTENDS:
				push_pending_annotations_to_head();
				advance();
				if (head->is_enum_file) {
					push_error(R"("enum_name" cannot be combined with "extends" in the same file.)");
				} else if (head->is_tuple_file) {
					push_error(R"("tuple_name" cannot be combined with "extends" in the same file.)");
				} else if (head->uses_used) {
					push_error(R"("extends" must appear before "uses".)");
				} else if (head->extends_used) {
					push_error(R"("extends" can only be used once.)");
				} else {
					parse_extends();
					if (match(BSTokenizer::Token::USES)) {
						parse_uses();
						end_statement("uses declaration");
					} else {
						end_statement("superclass");
					}
				}
				break;
			case BSTokenizer::Token::USES:
				push_pending_annotations_to_head();
				advance();
				if (head->is_enum_file) {
					push_error(R"("enum_name" cannot be combined with "uses" in the same file.)");
				} else if (head->is_tuple_file) {
					push_error(R"("tuple_name" cannot be combined with "uses" in the same file.)");
				} else {
					parse_uses();
					end_statement("uses declaration");
				}
				break;
			case BSTokenizer::Token::TK_EOF:
				push_pending_annotations_to_head();
				can_have_class_or_extends = false;
				break;
			case BSTokenizer::Token::LITERAL:
				if (current.literal.get_type() == Variant::STRING) {
					// Allow strings in class body as multiline comments.
					advance();
					if (!match(BSTokenizer::Token::NEWLINE)) {
						push_error("Expected newline after comment string.");
					}
					break;
				}
				[[fallthrough]];
			default:
				// No tokens are allowed between script annotations and class/extends.
				can_have_class_or_extends = false;
				break;
		}

		if (panic_mode) {
			synchronize();
		}
	}

	// When the only thing needed is the class name, icon, and abstractness; we don't need to parse the whole file.
	// It really speed up the call to `BaristaScriptLanguage::get_global_class_name()` especially for large script.
	if (!parse_body) {
		return;
	}

	parse_class_body(true);

	head->end_line = current.end_line;
	head->end_column = current.end_column;

	complete_extents(head);

#ifdef TOOLS_ENABLED
	const HashMap<int, BSTokenizer::CommentData> &comments = tokenizer->get_comments();

	int max_line = head->end_line;
	if (!head->members.is_empty()) {
		max_line = MIN(max_line, head->members[0].get_line() - 1);
	}
	// `max_script_doc_line` is lowered past any doc comment already consumed by a member or an
	// annotation declaration. Clamp by it so those doc comments are never reused as the class
	// description, including in scripts whose only declarations are annotations (no members).
	max_line = MIN(max_line, max_script_doc_line);

	int line = 0;
	while (line <= max_line) {
		// Find the start.
		if (comments.has(line) && comments[line].new_line && comments[line].comment.begins_with("##")) {
			// Find the end.
			while (line + 1 <= max_line && comments.has(line + 1) && comments[line + 1].new_line && comments[line + 1].comment.begins_with("##")) {
				line++;
			}
			head->doc_data = parse_class_doc_comment(line);
			break;
		}
		line++;
	}
#endif // TOOLS_ENABLED

	if (!check(BSTokenizer::Token::TK_EOF)) {
		push_error("Expected end of file.");
	}

	clear_unused_annotations();
}

// D-deferred: `get_depended_parser_for()` / `get_depended_parsers()` (fs_parser.cpp:1119-1136 @
// c9d5e35) hand the analyzer the parsers of the files this one depends on, through `FSParserRef` and
// `FSCache::get_parser()`. Both are the parse-tree halves of the cache that `bs_cache.h:29-31`
// deliberately leaves to M3, and the parser itself never calls either.

List<String> BSParser::get_dependencies() const {
	List<String> dependencies;
	HashSet<String> seen;

	auto add_dependency = [&](String p_path) {
		p_path = p_path.strip_edges();
		if (p_path.is_empty()) {
			return;
		}
		if (p_path.is_relative_path() && !script_path.is_empty()) {
			p_path = script_path.get_base_dir().path_join(p_path).simplify_path();
		}
		p_path = ResourceUID::ensure_path(p_path);
		if (seen.has(p_path)) {
			return;
		}
		seen.insert(p_path);
		dependencies.push_back(p_path);
	};

	for (const Node *node = list; node != nullptr; node = node->next) {
		switch (node->type) {
			case Node::CLASS: {
				const ClassNode *class_node = static_cast<const ClassNode *>(node);
				if (!class_node->extends_path.is_empty()) {
					add_dependency(class_node->extends_path);
				}
			} break;
			case Node::PRELOAD: {
				const PreloadNode *preload = static_cast<const PreloadNode *>(node);
				if (preload->path != nullptr && preload->path->type == Node::LITERAL) {
					const LiteralNode *literal = static_cast<const LiteralNode *>(preload->path);
					if (literal->value.get_type() == Variant::STRING) {
						add_dependency(literal->value);
					}
				} else if (!preload->resolved_path.is_empty() && preload->resolved_path != "<missing path>") {
					add_dependency(preload->resolved_path);
				}
			} break;
			default:
				break;
		}
	}

	for (const String &conformance_path : get_namespace_conformance_dependencies()) {
		add_dependency(conformance_path);
	}

	return dependencies;
}

List<String> BSParser::get_namespace_conformance_dependencies() const {
	// A retroactive conformance takes effect for code that loads its declaring file. A conformance-only
	// file exports no global class, so name resolution alone never reaches it — which is why an
	// `import` of the namespace it lives in is what establishes the edge, and why a file's own
	// namespace counts as implicitly imported (the same rule short names and custom annotations
	// follow). The project-wide index is the only thing that knows such a file exists; it is filled by
	// the same file-system scan that indexes global classes.
	//
	// The global namespace is deliberately excluded. It has no `import` syntax, so membership in it is
	// not a choice a file makes — every file with no `namespace` declaration is in it. Treating it as
	// implicitly imported would make every script in a project load every other one that happens to
	// declare a conformance, whatever else that file does. An unnamespaced conformance is therefore
	// reached only by loading its declaring file explicitly.
	List<String> conformance_paths;
	if (head == nullptr) {
		return conformance_paths;
	}
	const BSParserHost *host = BSParserHost::get_singleton();

	// A build task bootstrap compiles inside a dependency root it may not reach outside of. Namespace
	// membership is not something the bootstrapped file opts into per dependency, so an out-of-root
	// conformance file is filtered out here rather than reported: it is simply not reachable, so
	// nothing type-checks against it and nothing loads it. An explicit `import` of a namespace whose
	// conformances lie outside the root is still an error (`validate_bootstrap_namespace_import`),
	// because there the file did ask for it.
	HashSet<String> seen_namespaces;
	auto collect_namespace = [&](const String &p_namespace) {
		if (p_namespace.is_empty() || seen_namespaces.has(p_namespace)) {
			return;
		}
		seen_namespaces.insert(p_namespace);
		for (const String &path : host->get_conformance_files_in_namespace(p_namespace)) {
			// A file never depends on itself, and a conformance it declares is already registered by
			// its own analysis.
			if (path == script_path) {
				continue;
			}
			if (!host->is_bootstrap_path_allowed(path)) {
				continue;
			}
			conformance_paths.push_back(path);
		}
	};

	collect_namespace(head->namespace_name);
	for (const String &import : head->imports) {
		collect_namespace(import);
	}
	return conformance_paths;
}

BSParser::ClassNode *BSParser::find_class(const String &p_qualified_name) const {
	if (p_qualified_name == head->fqcn) {
		return head;
	}

	String first = p_qualified_name.get_slice("::", 0);

	PackedStringArray class_names;
	BSParser::ClassNode *result = nullptr;
	// Empty initial name means start at the head.
	if (first.is_empty() || (head->identifier && first == head->identifier->name)) {
		class_names = p_qualified_name.split("::");
		result = head;
	} else if (p_qualified_name.begins_with(script_path)) {
		// Script path could have a class path separator("::") in it.
		class_names = p_qualified_name.trim_prefix(script_path).split("::");
		result = head;
	} else if (head->has_member(first)) {
		class_names = p_qualified_name.split("::");
		BSParser::ClassNode::Member member = head->get_member(first);
		if (member.type == BSParser::ClassNode::Member::CLASS) {
			result = member.m_class;
		}
	}

	// Starts at index 1 because index 0 was handled above.
	for (int i = 1; result != nullptr && i < class_names.size(); i++) {
		const String &current_name = class_names[i];
		BSParser::ClassNode *next = nullptr;
		if (result->has_member(current_name)) {
			BSParser::ClassNode::Member member = result->get_member(current_name);
			if (member.type == BSParser::ClassNode::Member::CLASS) {
				next = member.m_class;
			}
		}
		result = next;
	}

	return result;
}

bool BSParser::has_class(const BSParser::ClassNode *p_class) const {
	if (head->fqcn.is_empty() && p_class->fqcn.get_slice("::", 0).is_empty()) {
		return p_class == head;
	} else if (p_class->fqcn.begins_with(head->fqcn)) {
		return find_class(p_class->fqcn.trim_prefix(head->fqcn)) == p_class;
	}

	return false;
}

bool BSParser::parse_identifier_chain(const String &p_declaration_name, String &r_chain) {
	if (p_declaration_name == "import") {
		make_completion_context(COMPLETION_IMPORT_NAMESPACE, nullptr);
	}
	if (!consume(BSTokenizer::Token::IDENTIFIER, vformat(R"(Expected identifier after "%s".)", p_declaration_name))) {
		return false;
	}

	r_chain = parse_identifier()->name;

	while (match(BSTokenizer::Token::PERIOD)) {
		if (p_declaration_name == "import") {
			make_completion_context(COMPLETION_IMPORT_NAMESPACE, nullptr);
		}
		if (!consume(BSTokenizer::Token::IDENTIFIER, vformat(R"(Expected identifier after "." in %s declaration.)", p_declaration_name))) {
			return false;
		}
		r_chain += "." + String(parse_identifier()->name);
	}

	return true;
}

void BSParser::parse_namespace() {
	bool can_store_namespace = true;
	if (!head->namespace_name.is_empty()) {
		push_error(R"("namespace" can only be used once.)");
		can_store_namespace = false;
	} else if (!head->imports.is_empty()) {
		push_error(R"("namespace" must be declared before "import".)");
		can_store_namespace = false;
	}

	String namespace_name;
	if (parse_identifier_chain("namespace", namespace_name) && can_store_namespace) {
		head->namespace_name = namespace_name;
	}

	if (!panic_mode) {
		end_statement("namespace declaration");
	}
}

void BSParser::parse_import() {
	String import;
	if (parse_identifier_chain("import", import)) {
		head->imports.push_back(import);
	}

	if (!panic_mode) {
		end_statement("import declaration");
	}
}

BSParser::ClassNode *BSParser::parse_class(const DeclarationModifiers &p_modifiers) {
	ClassNode *n_class = alloc_node<ClassNode>();

	ClassNode *previous_class = current_class;
	current_class = n_class;
	n_class->outer = previous_class;
	n_class->is_abstract = p_modifiers.is_abstract;
	n_class->is_final = p_modifiers.is_final;

	reject_reserved_type_name();
	if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected identifier for the class name after "class".)")) {
		n_class->identifier = parse_identifier();
		if (n_class->outer) {
			String fqcn = n_class->outer->fqcn;
			if (fqcn.is_empty()) {
				fqcn = BaristaScript::canonicalize_path(script_path);
			}
			n_class->fqcn = fqcn + "::" + n_class->identifier->name;
		} else {
			n_class->fqcn = n_class->identifier->name;
		}
	}

	parse_type_parameters(n_class->type_parameters);

	if (match(BSTokenizer::Token::EXTENDS)) {
		parse_extends();
	}

	if (match(BSTokenizer::Token::USES)) {
		parse_uses();
	}

	consume(BSTokenizer::Token::COLON, R"(Expected ":" after class declaration.)");

	bool multiline = match(BSTokenizer::Token::NEWLINE);

	if (multiline && !consume(BSTokenizer::Token::INDENT, R"(Expected indented block after class declaration.)")) {
		current_class = previous_class;
		complete_extents(n_class);
		return n_class;
	}

	if (match(BSTokenizer::Token::EXTENDS)) {
		if (n_class->extends_used) {
			push_error(R"(Cannot use "extends" more than once in the same class.)");
		}
		parse_extends();
		if (match(BSTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("superclass");
		}
	} else if (match(BSTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	}

	parse_class_body(multiline);
	complete_extents(n_class);

	if (multiline) {
		consume(BSTokenizer::Token::DEDENT, R"(Missing unindent at the end of the class body.)");
	}

	current_class = previous_class;
	return n_class;
}

BSParser::TraitNode *BSParser::parse_trait(const DeclarationModifiers &p_modifiers) {
	TraitNode *trait = alloc_node<TraitNode>();

	ClassNode *previous_class = current_class;
	current_class = trait;
	trait->outer = previous_class;
	trait->is_abstract = p_modifiers.is_abstract;

	reject_reserved_type_name();
	if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected identifier for the trait name after "trait".)")) {
		trait->identifier = parse_identifier();
		if (trait->outer) {
			String fqcn = trait->outer->fqcn;
			if (fqcn.is_empty()) {
				fqcn = BaristaScript::canonicalize_path(script_path);
			}
			trait->fqcn = fqcn + "::" + trait->identifier->name;
		} else {
			trait->fqcn = trait->identifier->name;
		}
	}

	// A trait may be generic (`trait Container[T]`), mirroring `class Box[T]`.
	parse_type_parameters(trait->type_parameters);

	if (match(BSTokenizer::Token::EXTENDS)) {
		parse_extends();
	}

	if (match(BSTokenizer::Token::USES)) {
		parse_uses();
	}

	consume(BSTokenizer::Token::COLON, R"(Expected ":" after trait declaration.)");

	bool multiline = match(BSTokenizer::Token::NEWLINE);

	if (multiline && !consume(BSTokenizer::Token::INDENT, R"(Expected indented block after trait declaration.)")) {
		current_class = previous_class;
		complete_extents(trait);
		return trait;
	}

	if (match(BSTokenizer::Token::EXTENDS)) {
		if (trait->extends_used) {
			push_error(R"(Cannot use "extends" more than once in the same trait.)");
		}
		parse_extends();
		if (match(BSTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("supertrait");
		}
	} else if (match(BSTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	}

	parse_class_body(multiline);
	complete_extents(trait);

	if (multiline) {
		consume(BSTokenizer::Token::DEDENT, R"(Missing unindent at the end of the trait body.)");
	}

	current_class = previous_class;
	return trait;
}

void BSParser::parse_class_name() {
	reject_reserved_type_name();
	if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected identifier for the global class name after "class_name".)")) {
		current_class->identifier = parse_identifier();
		current_class->qualified_global_name = current_class->namespace_name.is_empty() ? String(current_class->identifier->name) : current_class->namespace_name + "." + String(current_class->identifier->name);
		current_class->fqcn = current_class->qualified_global_name;
	}

	parse_type_parameters(current_class->type_parameters);

	if (match(BSTokenizer::Token::EXTENDS)) {
		// Allow extends on the same line.
		parse_extends();
		if (match(BSTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("superclass");
		}
	} else if (match(BSTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	} else {
		end_statement("class_name statement");
	}
}

void BSParser::parse_trait_name() {
	reject_reserved_type_name();
	if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected identifier for the global trait name after "trait_name".)")) {
		current_class->is_trait = true;
		current_class->trait_name_used = true;
		current_class->identifier = parse_identifier();
		current_class->qualified_global_name = current_class->namespace_name.is_empty() ? String(current_class->identifier->name) : current_class->namespace_name + "." + String(current_class->identifier->name);
		current_class->fqcn = current_class->qualified_global_name;
	}

	// A global trait may be generic (`trait_name Container[T]`), mirroring `class_name Box[T]`.
	parse_type_parameters(current_class->type_parameters);

	if (match(BSTokenizer::Token::EXTENDS)) {
		// Allow extends on the same line.
		parse_extends();
		if (match(BSTokenizer::Token::USES)) {
			parse_uses();
			end_statement("uses declaration");
		} else {
			end_statement("supertrait");
		}
	} else if (match(BSTokenizer::Token::USES)) {
		parse_uses();
		end_statement("uses declaration");
	} else {
		end_statement("trait_name statement");
	}
}

void BSParser::parse_enum_name(bool p_can_register_enum_file) {
	const bool already_enum_file = current_class->is_enum_file;
	bool has_conflict = !p_can_register_enum_file;
	if (already_enum_file) {
		push_error(R"("enum_name" can only be used once per file.)");
		has_conflict = true;
	} else if (!current_class->annotations.is_empty()) {
		push_error(R"(An "enum_name" file may only contain its enum declaration.)");
		has_conflict = true;
	} else if (current_class->trait_name_used) {
		push_error(R"("enum_name" cannot be combined with "trait_name" in the same file.)");
		has_conflict = true;
	} else if (current_class->identifier != nullptr) {
		push_error(R"("enum_name" cannot be combined with "class_name" in the same file.)");
		has_conflict = true;
	} else if (current_class->extends_used) {
		push_error(R"("enum_name" cannot be combined with "extends" in the same file.)");
		has_conflict = true;
	} else if (current_class->uses_used) {
		push_error(R"("enum_name" cannot be combined with "uses" in the same file.)");
		has_conflict = true;
	}

	DeclarationModifiers no_modifiers;
	EnumNode *enum_node = parse_enum(no_modifiers);
	if (!has_conflict) {
		current_class->is_enum_file = true;
		current_class->enum_file_decl = enum_node;
	}

	if (!has_conflict && enum_node != nullptr && enum_node->identifier != nullptr) {
		current_class->identifier = enum_node->identifier;
		current_class->qualified_global_name = current_class->namespace_name.is_empty() ? String(current_class->identifier->name) : current_class->namespace_name + "." + String(current_class->identifier->name);
		current_class->fqcn = current_class->qualified_global_name;
	}
}

void BSParser::parse_tuple_name(bool p_can_register_tuple_file) {
	const bool already_tuple_file = current_class->is_tuple_file;
	bool has_conflict = !p_can_register_tuple_file;
	if (already_tuple_file) {
		push_error(R"("tuple_name" can only be used once per file.)");
		has_conflict = true;
	} else if (!current_class->annotations.is_empty()) {
		push_error(R"(A "tuple_name" file may only contain its tuple declaration.)");
		has_conflict = true;
	} else if (current_class->is_enum_file) {
		push_error(R"("tuple_name" cannot be combined with "enum_name" in the same file.)");
		has_conflict = true;
	} else if (current_class->trait_name_used) {
		push_error(R"("tuple_name" cannot be combined with "trait_name" in the same file.)");
		has_conflict = true;
	} else if (current_class->identifier != nullptr) {
		push_error(R"("tuple_name" cannot be combined with "class_name" in the same file.)");
		has_conflict = true;
	} else if (current_class->extends_used) {
		push_error(R"("tuple_name" cannot be combined with "extends" in the same file.)");
		has_conflict = true;
	} else if (current_class->uses_used) {
		push_error(R"("tuple_name" cannot be combined with "uses" in the same file.)");
		has_conflict = true;
	}

	DeclarationModifiers no_modifiers;
	TupleNode *tuple_node = parse_tuple(no_modifiers);
	if (!has_conflict) {
		current_class->is_tuple_file = true;
		current_class->tuple_file_decl = tuple_node;
	}

	if (!has_conflict && tuple_node != nullptr && tuple_node->identifier != nullptr) {
		current_class->identifier = tuple_node->identifier;
		current_class->qualified_global_name = current_class->namespace_name.is_empty() ? String(current_class->identifier->name) : current_class->namespace_name + "." + String(current_class->identifier->name);
		current_class->fqcn = current_class->qualified_global_name;
	}
}

void BSParser::parse_extends() {
	current_class->extends_used = true;

	int chain_index = 0;

	// Type arguments specializing a generic base: `extends List[T]`, `extends List[int]`, or on a
	// path-based base, `extends "res://base.fs"[int]`.
	auto parse_extends_type_arguments = [this]() {
		if (match(BSTokenizer::Token::BRACKET_OPEN)) {
			if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
				push_error(R"(Expected at least one type argument after "[".)");
				// Still enter parse_type so `extends Base[<caret>]` records COMPLETION_TYPE_NAME.
				parse_type();
			} else {
				do {
					TypeNode *type_argument = parse_type();
					if (type_argument == nullptr) {
						push_error(R"(Expected type argument after "[".)");
						break;
					}
					current_class->extends_type_arguments.push_back(type_argument);
				} while (match(BSTokenizer::Token::COMMA));
			}
			consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after type arguments.)");
		}
	};

	if (match(BSTokenizer::Token::LITERAL)) {
		if (previous.literal.get_type() != Variant::STRING) {
			push_error(vformat(R"(Only strings or identifiers can be used after "extends", found "%s" instead.)", Variant::get_type_name(previous.literal.get_type())));
		}
		current_class->extends_path = previous.literal;

		if (!match(BSTokenizer::Token::PERIOD)) {
			// A path-based base with no `.Inner` chain can still carry type arguments.
			parse_extends_type_arguments();
			return;
		}
	}

	make_completion_context(COMPLETION_INHERIT_TYPE, current_class, chain_index++);

	reject_reserved_type_name();
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected superclass name after "extends".)")) {
		return;
	}
	current_class->extends.push_back(parse_identifier());

	while (match(BSTokenizer::Token::PERIOD)) {
		make_completion_context(COMPLETION_INHERIT_TYPE, current_class, chain_index++);
		reject_reserved_type_name();
		if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected superclass name after ".".)")) {
			return;
		}
		current_class->extends.push_back(parse_identifier());
	}

	parse_extends_type_arguments();
}

void BSParser::parse_uses() {
	if (current_class->uses_used) {
		push_error(vformat(R"(Cannot use "uses" more than once in the same %s.)", current_class->is_trait ? "trait" : "class"));
	}
	current_class->uses_used = true;

	do {
		ClassNode::TraitUse trait_use;
		if (!parse_trait_use(trait_use)) {
			return;
		}
		current_class->used_traits.push_back(trait_use);
	} while (match(BSTokenizer::Token::COMMA));
}

bool BSParser::parse_trait_use(ClassNode::TraitUse &r_trait_use) {
	int chain_index = 0;
	make_completion_context(COMPLETION_USES, current_class, chain_index++, true, &r_trait_use.name);
	reject_reserved_type_name();
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected trait name after "uses".)")) {
		return false;
	}
	r_trait_use.name.push_back(parse_identifier());

	while (match(BSTokenizer::Token::PERIOD)) {
		make_completion_context(COMPLETION_USES, current_class, chain_index++, true, &r_trait_use.name);
		reject_reserved_type_name();
		if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected trait name after ".".)")) {
			return false;
		}
		r_trait_use.name.push_back(parse_identifier());
	}

	// Type arguments specializing a generic trait: `uses Container[int]`.
	if (match(BSTokenizer::Token::BRACKET_OPEN)) {
		if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
			push_error(R"(Expected at least one type argument after "[".)");
			// Still enter parse_type so `uses Trait[<caret>]` records COMPLETION_TYPE_NAME.
			parse_type();
		} else {
			do {
				TypeNode *type_argument = parse_type();
				if (type_argument == nullptr) {
					push_error(R"(Expected type argument after "[".)");
					break;
				}
				r_trait_use.type_arguments.push_back(type_argument);
			} while (match(BSTokenizer::Token::COMMA));
		}
		consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after type arguments.)");
	}

	return true;
}

void BSParser::parse_type_parameters(Vector<TypeParameterNode *> &r_type_parameters) {
	if (!match(BSTokenizer::Token::BRACKET_OPEN)) {
		return;
	}

	if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
		push_error(R"(Expected type parameter name after "[".)");
	} else {
		do {
			if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			TypeParameterNode *type_parameter = alloc_node<TypeParameterNode>();
			reject_reserved_type_name();
			if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected type parameter name.)")) {
				type_parameter->identifier = parse_identifier();
				for (const TypeParameterNode *previous_parameter : r_type_parameters) {
					if (previous_parameter->identifier != nullptr && previous_parameter->identifier->name == type_parameter->identifier->name) {
						push_error(vformat(R"(Type parameter with name "%s" was already declared.)", type_parameter->identifier->name), type_parameter->identifier);
						break;
					}
				}
				if (match(BSTokenizer::Token::COLON)) {
					type_parameter->bound = parse_type();
					if (type_parameter->bound == nullptr) {
						push_error(R"(Expected type parameter bound after ":".)");
					}
				}
			}
			complete_extents(type_parameter);
			r_type_parameters.push_back(type_parameter);
		} while (match(BSTokenizer::Token::COMMA));
	}

	consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after type parameters.)");
}

List<BSParser::AnnotationNode *> BSParser::parse_class_member_annotations(AnnotationInfo::TargetKind p_target,
		const String &p_member_kind, const StringName &p_exclusive_builtin) {
	List<AnnotationNode *> annotations;
	while (!annotation_stack.is_empty()) {
		AnnotationNode *last_annotation = annotation_stack.back()->get();
		const bool is_allowed_builtin = p_exclusive_builtin == StringName() ||
				(!last_annotation->is_custom && last_annotation->name == p_exclusive_builtin);
		if (is_allowed_builtin && last_annotation->applies_to(p_target)) {
			annotations.push_front(last_annotation);
			annotation_stack.pop_back();
		} else {
			push_error(vformat(R"(Annotation "%s" cannot be applied to a %s.)", last_annotation->name, p_member_kind));
			clear_unused_annotations();
		}
	}
	return annotations;
}

template <typename T>
void BSParser::finalize_class_member(T *p_member, List<AnnotationNode *> &p_annotations, const String &p_member_kind) {
	if (p_member == nullptr) {
		return;
	}

#ifdef TOOLS_ENABLED
	int doc_comment_line = p_member->start_line - 1;
#endif // TOOLS_ENABLED

	for (AnnotationNode *&annotation : p_annotations) {
		p_member->annotations.push_back(annotation);
#ifdef TOOLS_ENABLED
		if (annotation->start_line <= doc_comment_line) {
			doc_comment_line = annotation->start_line - 1;
		}
#endif // TOOLS_ENABLED
	}

#ifdef TOOLS_ENABLED
	if constexpr (std::is_base_of_v<ClassNode, T>) {
		if (has_comment(p_member->start_line, true)) {
			// Inline doc comment.
			p_member->doc_data = parse_class_doc_comment(p_member->start_line, true);
		} else if (has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment. Don't check `min_member_doc_line` because a class ends parsing after its members.
			// This may not work correctly for cases like `var a; class B`, but it doesn't matter in practice.
			p_member->doc_data = parse_class_doc_comment(doc_comment_line);
		}
	} else {
		if (has_comment(p_member->start_line, true)) {
			// Inline doc comment.
			p_member->doc_data = parse_doc_comment(p_member->start_line, true);
		} else if (doc_comment_line >= min_member_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment.
			p_member->doc_data = parse_doc_comment(doc_comment_line);
		}
	}

	min_member_doc_line = p_member->end_line + 1; // Prevent multiple members from using the same doc comment.
#endif // TOOLS_ENABLED

	if (p_member->identifier != nullptr) {
		if (!((String)p_member->identifier->name).is_empty()) { // Enums may be unnamed.
			if (current_class->members_indices.has(p_member->identifier->name)) {
				push_error(vformat(R"(%s "%s" has the same name as a previously declared %s.)", p_member_kind.capitalize(), p_member->identifier->name, current_class->get_member(p_member->identifier->name).get_type_name()), p_member->identifier);
			} else {
				current_class->add_member(p_member);
			}
		} else {
			current_class->add_member(p_member);
		}
	}
}

template <typename T>
void BSParser::parse_class_member(T *(BSParser::*p_parse_function)(const DeclarationModifiers &),
		AnnotationInfo::TargetKind p_target, const String &p_member_kind,
		const DeclarationModifiers &p_modifiers, const StringName &p_exclusive_builtin) {
	advance();
	List<AnnotationNode *> annotations =
			parse_class_member_annotations(p_target, p_member_kind, p_exclusive_builtin);
	T *member = (this->*p_parse_function)(p_modifiers);
	finalize_class_member(member, annotations, p_member_kind);
}

void BSParser::parse_function_class_member(const DeclarationModifiers &p_modifiers) {
	advance();
	List<AnnotationNode *> annotations = parse_class_member_annotations(AnnotationInfo::FUNCTION, "function");
	FunctionNode *member = parse_function_declaration(p_modifiers);
	finalize_class_member(member, annotations, "function");
}

BSParser::AnnotationDeclarationNode *BSParser::parse_annotation_declaration() {
	AnnotationDeclarationNode *annotation_declaration = alloc_node<AnnotationDeclarationNode>();

	// The current token is the contextual `annotation` identifier. `alloc_node` anchored the
	// node to the preceding token, so re-anchor it to `annotation` itself once consumed; this
	// keeps analyzer diagnostics (duplicate/reserved identity) pointing at the declaration.
	advance();
	reset_extents(annotation_declaration, previous);

	// An annotation declaration is not a runtime member, so no class-level annotation may
	// apply to it. Consume any pending annotations here (erroring on each) so they cannot
	// silently carry over onto the next real member.
	parse_class_member_annotations(AnnotationInfo::NONE, "annotation declaration");

	// Annotation declarations are root-only in v1. Inner classes, traits, functions, and
	// local scopes never reach a valid declaration here.
	const bool is_root_declaration = current_class->outer == nullptr && !current_class->is_trait;
	if (!is_root_declaration) {
		push_error(R"(Annotation declarations are only allowed at the root of a script.)");
	}

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected annotation name after "annotation".)")) {
		complete_extents(annotation_declaration);
		return nullptr;
	}
	annotation_declaration->identifier = parse_identifier();

	if (match(BSTokenizer::Token::PARENTHESIS_OPEN)) {
		push_multiline(true);
		parse_annotation_declaration_parameters(annotation_declaration);
		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after annotation parameters.)*");
	}

	if (current.type == BSTokenizer::Token::IDENTIFIER && current.get_identifier() == StringName("targets")) {
		advance();
		parse_annotation_declaration_targets(annotation_declaration);
	} else {
		push_error(R"(Expected "targets" after the annotation name.)");
	}

	complete_extents(annotation_declaration);
	end_statement("annotation declaration");

	// Canonical identity uses the root namespace, which is parsed before the body.
	const String &namespace_name = head != nullptr ? head->namespace_name : String();
	annotation_declaration->qualified_name = namespace_name.is_empty()
			? String(annotation_declaration->identifier->name)
			: namespace_name + String(".") + String(annotation_declaration->identifier->name);

#ifdef TOOLS_ENABLED
	// Capture the doc comment so generated docs can describe the declaration. Annotation
	// declarations never carry leading `@annotations`, so the comment is on the line right
	// before the declaration (or inline on the same line).
	int doc_comment_line = annotation_declaration->start_line - 1;
	if (has_comment(annotation_declaration->start_line, true)) {
		// Inline doc comment.
		annotation_declaration->doc_data = parse_doc_comment(annotation_declaration->start_line, true);
	} else if (doc_comment_line >= min_member_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
		// Normal doc comment.
		annotation_declaration->doc_data = parse_doc_comment(doc_comment_line);
	}
	min_member_doc_line = annotation_declaration->end_line + 1; // Prevent reuse of the same doc comment.
#endif // TOOLS_ENABLED

	if (is_root_declaration) {
		current_class->annotation_declarations.push_back(annotation_declaration);
	}

	return annotation_declaration;
}

void BSParser::parse_annotation_declaration_parameters(AnnotationDeclarationNode *p_annotation_declaration) {
	if (check(BSTokenizer::Token::PARENTHESIS_CLOSE) || is_at_end()) {
		return;
	}

	bool default_used = false;
	do {
		if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
			break; // Allow for trailing comma.
		}

		bool is_rest = false;
		if (match(BSTokenizer::Token::PERIOD_PERIOD_PERIOD)) {
			is_rest = true;
		}

		ParameterNode *parameter = parse_parameter(false);
		if (parameter == nullptr) {
			break;
		}

		if (p_annotation_declaration->rest_parameter != nullptr) {
			push_error("Cannot have parameters after the variadic parameter.");
			continue;
		}

		if (parameter->initializer != nullptr) {
			if (is_rest) {
				push_error("The variadic parameter cannot have a default value.");
				continue;
			}
			default_used = true;
		} else if (default_used && !is_rest) {
			push_error("Cannot have mandatory parameters after optional parameters.");
			continue;
		}

		if (p_annotation_declaration->parameters_indices.has(parameter->identifier->name)) {
			push_error(vformat(R"(Parameter with name "%s" was already declared for this annotation.)", parameter->identifier->name));
		} else if (is_rest) {
			p_annotation_declaration->rest_parameter = parameter;
		} else {
			p_annotation_declaration->parameters_indices[parameter->identifier->name] = p_annotation_declaration->parameters.size();
			p_annotation_declaration->parameters.push_back(parameter);
		}
	} while (match(BSTokenizer::Token::COMMA));
}

void BSParser::parse_annotation_declaration_targets(AnnotationDeclarationNode *p_annotation_declaration) {
	do {
		uint32_t target_bit = AnnotationDeclarationNode::TARGET_NONE;
		String target_name;

		// Target names (`CLASS`, `METHOD`, `VARIABLE`, `SIGNAL`, `CONSTANT`) are uppercase, so they
		// arrive as ordinary identifiers rather than the lowercase keyword tokens.
		if (match(BSTokenizer::Token::IDENTIFIER)) {
			target_name = previous.get_identifier();
			if (target_name == "CLASS") {
				target_bit = AnnotationDeclarationNode::TARGET_CLASS;
			} else if (target_name == "METHOD") {
				target_bit = AnnotationDeclarationNode::TARGET_METHOD;
			} else if (target_name == "VARIABLE") {
				target_bit = AnnotationDeclarationNode::TARGET_VARIABLE;
			} else if (target_name == "SIGNAL") {
				target_bit = AnnotationDeclarationNode::TARGET_SIGNAL;
			} else if (target_name == "CONSTANT") {
				target_bit = AnnotationDeclarationNode::TARGET_CONSTANT;
			} else if (target_name == "PARAMETER") {
				target_bit = AnnotationDeclarationNode::TARGET_PARAMETER;
			} else {
				push_error(vformat(R"(Unknown annotation target "%s". Expected "CLASS", "METHOD", "VARIABLE", "SIGNAL", "CONSTANT", or "PARAMETER".)", target_name));
			}
		} else {
			push_error(R"(Expected an annotation target name.)");
			break;
		}

		if (target_bit != AnnotationDeclarationNode::TARGET_NONE) {
			if (p_annotation_declaration->targets & target_bit) {
				push_error(vformat(R"(Annotation target "%s" was already declared.)", target_name));
			} else {
				p_annotation_declaration->targets |= target_bit;
			}
		}
	} while (match(BSTokenizer::Token::COMMA));
}

BSParser::ConformanceNode *BSParser::parse_conformance() {
	ConformanceNode *conformance = alloc_node<ConformanceNode>();

	// The current token is the contextual `extend` identifier. `alloc_node` anchored the node
	// to the preceding token, so re-anchor it to `extend` itself once consumed.
	advance();
	reset_extents(conformance, previous);

	// A conformance is not a runtime member, so no class-level annotation may apply to it.
	// Consume any pending annotations here (erroring on each) so they cannot carry over.
	parse_class_member_annotations(AnnotationInfo::NONE, "conformance declaration");

	// Conformances are root-only, mirroring annotation declarations.
	const bool is_root_declaration = current_class->outer == nullptr && !current_class->is_trait;
	if (!is_root_declaration) {
		push_error(R"(Retroactive conformances ("extend") are only allowed at the root of a script.)");
	}

	// The target is an unspecialized dotted name. Type arguments are rejected because a
	// conformance applies to every specialization of a generic base.
	conformance->target = parse_type();
	if (conformance->target == nullptr) {
		push_error(R"(Expected a target type name after "extend".)");
	} else if (!conformance->target->container_types.is_empty() || conformance->target->has_signature || conformance->target->is_coroutine) {
		push_error(R"("extend" applies to all specializations; remove the type arguments.)", conformance->target);
	}

	if (consume(BSTokenizer::Token::USES, R"(Expected "uses" after the "extend" target type.)")) {
		parse_conformance_uses(conformance);
	}

	consume(BSTokenizer::Token::COLON, R"(Expected ":" after the "extend" conformance header.)");

	bool multiline = match(BSTokenizer::Token::NEWLINE);

	if (multiline && !consume(BSTokenizer::Token::INDENT, R"(Expected indented block after "extend" conformance declaration.)")) {
		complete_extents(conformance);
		if (is_root_declaration) {
			current_class->conformances.push_back(conformance);
		}
		return conformance;
	}

	parse_conformance_body(conformance, multiline);
	complete_extents(conformance);

	if (multiline) {
		consume(BSTokenizer::Token::DEDENT, R"(Missing unindent at the end of the "extend" conformance body.)");
	}

	if (is_root_declaration) {
		current_class->conformances.push_back(conformance);
	}

	return conformance;
}

void BSParser::parse_conformance_uses(ConformanceNode *p_conformance) {
	do {
		ClassNode::TraitUse trait_use;
		if (!parse_trait_use(trait_use)) {
			return;
		}
		p_conformance->traits.push_back(trait_use);
	} while (match(BSTokenizer::Token::COMMA));
}

void BSParser::parse_conformance_body(ConformanceNode *p_conformance, bool p_is_multiline) {
	bool body_end = false;
	while (!body_end && !is_at_end()) {
		DeclarationModifiers modifiers = collect_declaration_modifiers();

		BSTokenizer::Token token = current;
		switch (token.type) {
			case BSTokenizer::Token::FUNC: {
				validate_declaration_modifiers(modifiers, "functions", true, true, true, true, false);
				advance();
				FunctionNode *function = parse_function_declaration(modifiers);
				if (function != nullptr) {
					p_conformance->witnesses.push_back(function);
				}
			} break;
			case BSTokenizer::Token::PASS:
				advance();
#ifdef TOOLS_ENABLED
				p_conformance->erased_pass_lines.push_back(token.start_line);
#endif // TOOLS_ENABLED
				end_statement(R"("pass")");
				break;
			case BSTokenizer::Token::DEDENT:
				body_end = true;
				break;
			default:
				push_error(R"(An "extend" conformance body may only contain methods.)");
				advance();
				break;
		}
		if (panic_mode) {
			synchronize();
		}
		if (!p_is_multiline) {
			body_end = true;
		}
	}
}

BSParser::DeclarationModifiers BSParser::collect_declaration_modifiers() {
	DeclarationModifiers modifiers;
	while (true) {
		if (check(BSTokenizer::Token::FINAL)) {
			advance();
			if (modifiers.is_final) {
				push_error(R"(The "final" modifier was already specified.)");
			}
			modifiers.is_final = true;
			modifiers.final_line = previous.start_line;
			modifiers.final_column = previous.start_column;
		} else if (check(BSTokenizer::Token::ABSTRACT)) {
			advance();
			if (modifiers.is_abstract) {
				push_error(R"(The "abstract" modifier was already specified.)");
			}
			modifiers.is_abstract = true;
			modifiers.abstract_line = previous.start_line;
			modifiers.abstract_column = previous.start_column;
		} else if (check(BSTokenizer::Token::STATIC)) {
			advance();
			if (modifiers.is_static) {
				push_error(R"(The "static" modifier was already specified.)");
			}
			modifiers.is_static = true;
			modifiers.static_line = previous.start_line;
			modifiers.static_column = previous.start_column;
		} else if (current.type == BSTokenizer::Token::IDENTIFIER && current.get_identifier() == StringName("async")) {
			advance();
			if (modifiers.is_async) {
				push_error(R"(The "async" modifier was already specified.)");
			}
			modifiers.is_async = true;
			modifiers.async_line = previous.start_line;
			modifiers.async_column = previous.start_column;
		} else {
			break;
		}
	}
	return modifiers;
}

void BSParser::validate_declaration_modifiers(const DeclarationModifiers &p_modifiers, const char *p_target_kind, bool p_allow_abstract, bool p_allow_static, bool p_allow_async, bool p_allow_final, bool p_in_trait) {
	if (p_modifiers.is_final && !p_allow_final) {
		push_error(vformat(R"(The "final" modifier cannot be applied to %s.)", p_target_kind));
	}
	if (p_modifiers.is_abstract && !p_allow_abstract) {
		push_error(vformat(R"(The "abstract" modifier cannot be applied to %s.)", p_target_kind));
	}
	if (p_modifiers.is_static && !p_allow_static) {
		push_error(vformat(R"(The "static" modifier cannot be applied to %s.)", p_target_kind));
	}
	if (p_modifiers.is_async && !p_allow_async) {
		push_error(vformat(R"(The "async" modifier cannot be applied to %s.)", p_target_kind));
	}
	// Combination rules only apply where each modifier is individually valid.
	if (p_allow_abstract && p_allow_static && p_modifiers.is_abstract && p_modifiers.is_static && !p_in_trait) {
		push_error(R"(The "abstract" and "static" modifiers cannot be combined outside a trait.)");
	}
	// `final` and `abstract` are contradictory on both classes and methods: `final` forbids
	// extension/override, `abstract` requires it.
	if (p_allow_final && p_allow_abstract && p_modifiers.is_final && p_modifiers.is_abstract) {
		push_error(R"(The "final" and "abstract" modifiers cannot be combined.)");
	}
	// `abstract async` is intentionally allowed: it declares an async contract that
	// forces overriding implementations to be async. `final static` is also allowed.
}

void BSParser::parse_class_body(bool p_is_multiline) {
	bool class_end = false;
	while (!class_end && !is_at_end()) {
		DeclarationModifiers modifiers = collect_declaration_modifiers();
		const bool in_trait = current_class != nullptr && current_class->is_trait;

		BSTokenizer::Token token = current;
		const bool starts_annotation_declaration = token.type == BSTokenizer::Token::IDENTIFIER &&
				token.get_identifier() == StringName("annotation");
		const bool starts_conformance_declaration = token.type == BSTokenizer::Token::IDENTIFIER &&
				token.get_identifier() == StringName("extend");
		const bool starts_alias_declaration = starts_type_alias_declaration();
		const bool starts_declaration = token.type == BSTokenizer::Token::VAR ||
				token.type == BSTokenizer::Token::TK_CONST ||
				token.type == BSTokenizer::Token::SIGNAL ||
				token.type == BSTokenizer::Token::FUNC ||
				token.type == BSTokenizer::Token::CLASS ||
				token.type == BSTokenizer::Token::TRAIT ||
				token.type == BSTokenizer::Token::ENUM ||
				token.type == BSTokenizer::Token::TUPLE;
		const bool disallowed_enum_file_member = current_class->is_enum_file &&
				(token.type == BSTokenizer::Token::VAR ||
						token.type == BSTokenizer::Token::TK_CONST ||
						token.type == BSTokenizer::Token::SIGNAL ||
						token.type == BSTokenizer::Token::FUNC ||
						token.type == BSTokenizer::Token::CLASS ||
						token.type == BSTokenizer::Token::TRAIT ||
						token.type == BSTokenizer::Token::ENUM ||
						token.type == BSTokenizer::Token::TUPLE ||
						token.type == BSTokenizer::Token::ANNOTATION ||
						token.type == BSTokenizer::Token::PASS ||
						starts_annotation_declaration ||
						starts_conformance_declaration ||
						starts_alias_declaration);
		if (disallowed_enum_file_member) {
			push_error(R"(An "enum_name" file may only contain its enum declaration.)");
		}
		// A `tuple_name` file declares one global tuple type and nothing else, mirroring `enum_name`:
		// the file has no script body to attach members to.
		const bool disallowed_tuple_file_member = current_class->is_tuple_file &&
				(starts_declaration ||
						token.type == BSTokenizer::Token::ANNOTATION ||
						token.type == BSTokenizer::Token::PASS ||
						starts_annotation_declaration ||
						starts_conformance_declaration ||
						starts_alias_declaration);
		if (disallowed_tuple_file_member) {
			push_error(R"(A "tuple_name" file may only contain its tuple declaration.)");
		}
		if (modifiers.has_any() && !starts_declaration) {
			push_error(R"(Expected a declaration after the modifier.)");
		}

		switch (token.type) {
			case BSTokenizer::Token::VAR:
				validate_declaration_modifiers(modifiers, "variables", false, true, false, true, in_trait);
				parse_class_member(&BSParser::parse_variable, AnnotationInfo::VARIABLE, "variable", modifiers);
				if (modifiers.is_static) {
					current_class->has_static_data = true;
				}
				break;
			case BSTokenizer::Token::TK_CONST:
				validate_declaration_modifiers(modifiers, "constants", false, false, false, false, in_trait);
				parse_class_member(&BSParser::parse_constant, AnnotationInfo::CONSTANT, "constant", modifiers);
				break;
			case BSTokenizer::Token::SIGNAL:
				validate_declaration_modifiers(modifiers, "signals", false, false, false, false, in_trait);
				parse_class_member(&BSParser::parse_signal, AnnotationInfo::SIGNAL, "signal", modifiers);
				break;
			case BSTokenizer::Token::FUNC:
				validate_declaration_modifiers(modifiers, "functions", true, true, true, true, in_trait);
				parse_function_class_member(modifiers);
				break;
			case BSTokenizer::Token::CLASS:
				validate_declaration_modifiers(modifiers, "classes", true, false, false, true, in_trait);
				parse_class_member(&BSParser::parse_class, AnnotationInfo::CLASS, "class", modifiers);
				break;
			case BSTokenizer::Token::TRAIT:
				validate_declaration_modifiers(modifiers, "traits", true, false, false, false, in_trait);
				parse_class_member(&BSParser::parse_trait, AnnotationInfo::CLASS, "trait", modifiers);
				break;
			case BSTokenizer::Token::ENUM:
				validate_declaration_modifiers(modifiers, "enums", false, false, false, false, in_trait);
				parse_class_member(&BSParser::parse_enum, AnnotationInfo::CONSTANT, "enum", modifiers,
						SNAME("@keep_name"));
				break;
			case BSTokenizer::Token::TUPLE:
				validate_declaration_modifiers(modifiers, "tuples", false, false, false, false, in_trait);
				parse_class_member(&BSParser::parse_tuple, AnnotationInfo::CONSTANT, "tuple", modifiers,
						SNAME("@keep_name"));
				break;
			case BSTokenizer::Token::ANNOTATION: {
				advance();

				// Check for class-level and standalone annotations.
				AnnotationNode *annotation = parse_annotation(AnnotationInfo::CLASS_LEVEL | AnnotationInfo::STANDALONE);
				if (annotation != nullptr) {
					if (annotation->applies_to(AnnotationInfo::STANDALONE)) {
						if (previous.type != BSTokenizer::Token::NEWLINE) {
							push_error(R"(Expected newline after a standalone annotation.)");
						}
						if (annotation->name == SNAME("@export_category") || annotation->name == SNAME("@export_group") || annotation->name == SNAME("@export_subgroup")) {
							current_class->add_member_group(annotation);
						} else if (annotation->name == SNAME("@warning_ignore_start") || annotation->name == SNAME("@warning_ignore_restore")) {
							// Some annotations need to be resolved and applied in the parser.
							annotation->apply(this, nullptr, nullptr);
						} else {
							push_error(R"(Unexpected standalone annotation.)");
						}
					} else { // `AnnotationInfo::CLASS_LEVEL`.
						annotation_stack.push_back(annotation);
					}
				}
				break;
			}
			case BSTokenizer::Token::PASS:
				advance();
#ifdef TOOLS_ENABLED
				if (current_class != nullptr) {
					current_class->erased_pass_lines.push_back(token.start_line);
				}
#endif // TOOLS_ENABLED
				end_statement(R"("pass")");
				break;
			case BSTokenizer::Token::NAMESPACE:
				advance();
				push_error(R"("namespace" declarations must appear before "import", "class_name", "extends", and body declarations.)");
				break;
			case BSTokenizer::Token::IMPORT:
				advance();
				push_error(R"("import" declarations must appear before "class_name", "extends", and body declarations.)");
				break;
			case BSTokenizer::Token::TRAIT_NAME:
				advance();
				push_error(R"("trait_name" declarations must appear before class body declarations.)");
				break;
			case BSTokenizer::Token::ENUM_NAME:
				advance();
				push_error(R"("enum_name" can only be used at the top of a file.)");
				break;
			case BSTokenizer::Token::EXTENDS:
				advance();
				if (current_class->uses_used) {
					push_error(R"("extends" must appear before "uses".)");
				} else {
					push_error(vformat(R"(Unexpected %s in class body.)", previous.get_debug_name()));
				}
				break;
			case BSTokenizer::Token::USES:
				advance();
				if (current_class->uses_used) {
					push_error(vformat(R"(Cannot use "uses" more than once in the same %s.)", current_class->is_trait ? "trait" : "class"));
				} else {
					push_error(R"("uses" declarations must appear before class body declarations.)");
				}
				break;
			case BSTokenizer::Token::DEDENT:
				class_end = true;
				break;
			case BSTokenizer::Token::LITERAL:
				if (current.literal.get_type() == Variant::STRING) {
					// Allow strings in class body as multiline comments.
					advance();
					if (!match(BSTokenizer::Token::NEWLINE)) {
						push_error("Expected newline after comment string.");
					}
					break;
				}
				[[fallthrough]];
			default:
				if (token.type == BSTokenizer::Token::IDENTIFIER && token.get_identifier() == StringName("annotation")) {
					// `annotation` is contextual: it only starts a declaration where a root-body
					// declaration is valid. Anywhere else it remains an ordinary identifier.
					parse_annotation_declaration();
					break;
				}
				if (token.type == BSTokenizer::Token::IDENTIFIER && token.get_identifier() == StringName("extend")) {
					// `extend` is contextual: it only starts a retroactive conformance where a
					// root-body declaration is valid. Anywhere else it remains an ordinary identifier.
					parse_conformance();
					break;
				}
				if (starts_alias_declaration) {
					// `type` is contextual: only the sequence `type IDENTIFIER` at a declaration
					// position starts an alias. Anywhere else it remains an ordinary identifier.
					TypeAliasNode *type_alias = parse_type_alias();
					if (type_alias != nullptr && type_alias->identifier != nullptr) {
						// An alias shares the class's member name space, so it collides with a
						// variable, constant, or nested type of the same name exactly as those
						// collide with each other (see `finalize_class_member`).
						if (current_class->members_indices.has(type_alias->identifier->name)) {
							push_error(vformat(R"(Type alias "%s" has the same name as a previously declared %s.)",
											   type_alias->identifier->name,
											   current_class->get_member(type_alias->identifier->name).get_type_name()),
									type_alias->identifier);
						} else {
							current_class->add_member(type_alias);
						}
					}
					break;
				}
				// Display a completion with declaration-oriented identifiers.
				make_completion_context(COMPLETION_DECLARATION, nullptr);
				advance();
				if (previous.get_identifier() == "export") {
					push_error(R"(The "export" keyword was removed in Godot 4. Use an export annotation ("@export", "@export_range", etc.) instead.)");
				} else if (previous.get_identifier() == "tool") {
					push_error(R"(The "tool" keyword was removed in Godot 4. Use the "@tool" annotation instead.)");
				} else if (previous.get_identifier() == "onready") {
					push_error(R"(The "onready" keyword was removed in Godot 4. Use the "@onready" annotation instead.)");
				} else if (previous.get_identifier() == "remote") {
					push_error(R"(The "remote" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" instead.)");
				} else if (previous.get_identifier() == "remotesync") {
					push_error(R"(The "remotesync" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" and "call_local" instead.)");
				} else if (previous.get_identifier() == "puppet") {
					push_error(R"(The "puppet" keyword was removed in Godot 4. Use the "@rpc" annotation with "authority" instead.)");
				} else if (previous.get_identifier() == "puppetsync") {
					push_error(R"(The "puppetsync" keyword was removed in Godot 4. Use the "@rpc" annotation with "authority" and "call_local" instead.)");
				} else if (previous.get_identifier() == "master") {
					push_error(R"(The "master" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" and perform a check inside the function instead.)");
				} else if (previous.get_identifier() == "mastersync") {
					push_error(R"(The "mastersync" keyword was removed in Godot 4. Use the "@rpc" annotation with "any_peer" and "call_local", and perform a check inside the function instead.)");
				} else {
					push_error(vformat(R"(Unexpected %s in class body.)", previous.get_debug_name()));
				}
				break;
		}
		if (panic_mode) {
			synchronize();
		}
		if (!p_is_multiline) {
			class_end = true;
		}
	}
}

BSParser::VariableNode *BSParser::parse_variable(const DeclarationModifiers &p_modifiers) {
	return parse_variable(p_modifiers.is_static, true, p_modifiers.is_final);
}

BSParser::VariableNode *BSParser::parse_variable(bool p_is_static, bool p_allow_property, bool p_is_final) {
	VariableNode *variable = alloc_node<VariableNode>();

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected variable name after "var".)")) {
		complete_extents(variable);
		return nullptr;
	}

	variable->identifier = parse_identifier();
	variable->export_info.name = variable->identifier->name;
	variable->is_static = p_is_static;
	variable->is_final = p_is_final;

	if (match(BSTokenizer::Token::COLON)) {
		if (p_allow_property) {
			make_completion_context(COMPLETION_PROPERTY_DECLARATION_OR_TYPE, variable);
		}
		if (check(BSTokenizer::Token::NEWLINE)) {
			if (p_allow_property) {
				advance();
				return parse_property(variable, true);
			} else {
				push_error(R"(Expected type after ":")");
				complete_extents(variable);
				return nullptr;
			}
		} else if (check((BSTokenizer::Token::EQUAL))) {
			// Infer type.
			variable->infer_datatype = true;
		} else {
			if (p_allow_property) {
				if (check(BSTokenizer::Token::IDENTIFIER)) {
					// Check if get or set.
					if (current.get_identifier() == "get" || current.get_identifier() == "set") {
						return parse_property(variable, false);
					}
				}
			}

			// Parse type.
			variable->datatype_specifier = parse_type();
			if (variable->datatype_specifier == nullptr) {
				push_error(R"(Expected type after ":")");
			}
		}
	}

	if (match(BSTokenizer::Token::EQUAL)) {
		// Initializer.
		variable->initializer = parse_expression(false);
		if (variable->initializer == nullptr) {
			push_error(R"(Expected expression for variable initial value after "=".)");
		}
		variable->assignments++;
	}

	if (p_allow_property && match(BSTokenizer::Token::COLON)) {
		if (match(BSTokenizer::Token::NEWLINE)) {
			return parse_property(variable, true);
		} else {
			return parse_property(variable, false);
		}
	}

	complete_extents(variable);
	end_statement("variable declaration");

	return variable;
}

// Parses `var (x, y) = expr` / `const (x, _) = expr`, entered with `(` as the current token. Each
// name becomes a plain local so the rest of the front-end treats destructured bindings exactly like
// ordinary locals; `_` bindings are stored as null slots and never enter the scope.
BSParser::VariableDestructureNode *BSParser::parse_variable_destructure(bool p_is_const) {
	VariableDestructureNode *destructure = alloc_node<VariableDestructureNode>();
	destructure->is_const = p_is_const;

	advance(); // Past "(".

	bool bindings_valid = true;
	if (!check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
		do {
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				break; // Trailing comma.
			}
			if (match(BSTokenizer::Token::UNDERSCORE)) {
				destructure->bindings.push_back(nullptr);
				continue;
			}
			if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected a binding name or "_" in the destructuring declaration.)")) {
				bindings_valid = false;
				break;
			}
			VariableNode *binding = alloc_node<VariableNode>();
			reset_extents(binding, previous);
			binding->identifier = parse_identifier();
			binding->export_info.name = binding->identifier->name;
			// A `const` binding is written exactly once, by this declaration; reuse the `final`
			// analysis that already rejects every later write.
			binding->is_final = p_is_const;
			binding->infer_datatype = true;
			complete_extents(binding);
			destructure->bindings.push_back(binding);
		} while (match(BSTokenizer::Token::COMMA));
	}

	if (bindings_valid) {
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after the destructuring binding list.)*");
		if (destructure->bindings.size() < 2) {
			push_error("A destructuring declaration must bind at least two elements.");
		} else if (consume(BSTokenizer::Token::EQUAL, R"(Expected "=" after the destructuring binding list.)")) {
			destructure->initializer = parse_expression(false);
			if (destructure->initializer == nullptr) {
				push_error(R"(Expected expression for the destructuring initial value after "=".)");
			}
			for (int i = 0; i < destructure->bindings.size(); i++) {
				if (destructure->bindings[i] != nullptr) {
					// The declaration writes every binding, exactly like an ordinary initialized
					// local, so later reads and compound assignments are not "unassigned".
					destructure->bindings[i]->assignments++;
				}
			}
		}
	}

	complete_extents(destructure);
	end_statement("destructuring declaration");

	return destructure;
}

BSParser::VariableNode *BSParser::parse_property(VariableNode *p_variable, bool p_need_indent) {
	if (p_need_indent) {
		if (!consume(BSTokenizer::Token::INDENT, R"(Expected indented block for property after ":".)")) {
			complete_extents(p_variable);
			return nullptr;
		}
	}

	VariableNode *property = p_variable;

	make_completion_context(COMPLETION_PROPERTY_DECLARATION, property);

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected "get" or "set" for property declaration.)")) {
		complete_extents(p_variable);
		return nullptr;
	}

	IdentifierNode *function = parse_identifier();

	if (check(BSTokenizer::Token::EQUAL)) {
		p_variable->property = VariableNode::PROP_SETGET;
	} else {
		p_variable->property = VariableNode::PROP_INLINE;
		if (!p_need_indent) {
			push_error("Property with inline code must go to an indented block.");
		}
	}

	bool getter_used = false;
	bool setter_used = false;

	// Run with a loop because order doesn't matter.
	for (int i = 0; i < 2; i++) {
		if (function->name == SNAME("set")) {
			if (setter_used) {
				push_error(R"(Properties can only have one setter.)");
			} else {
				parse_property_setter(property);
				setter_used = true;
			}
		} else if (function->name == SNAME("get")) {
			if (getter_used) {
				push_error(R"(Properties can only have one getter.)");
			} else {
				parse_property_getter(property);
				getter_used = true;
			}
		} else {
			if (setter_used && !getter_used) {
				push_error(R"(Expected "get" for property declaration.)");
			} else if (getter_used && !setter_used) {
				push_error(R"(Expected "set" for property declaration.)");
			} else {
				push_error(R"(Expected "get" or "set" for property declaration.)");
			}
		}

		if (i == 0 && p_variable->property == VariableNode::PROP_SETGET) {
			if (match(BSTokenizer::Token::COMMA)) {
				// Consume potential newline.
				if (match(BSTokenizer::Token::NEWLINE)) {
					if (!p_need_indent) {
						push_error(R"(Inline setter/getter setting cannot span across multiple lines (use "\\"" if needed).)");
					}
				}
			} else {
				break;
			}
		}

		if (!match(BSTokenizer::Token::IDENTIFIER)) {
			break;
		}
		function = parse_identifier();
	}
	complete_extents(p_variable);

	if (p_variable->property == VariableNode::PROP_SETGET) {
		end_statement("property declaration");
	}

	if (p_need_indent) {
		consume(BSTokenizer::Token::DEDENT, R"(Expected end of indented block for property.)");
	}
	return property;
}

void BSParser::parse_property_setter(VariableNode *p_variable) {
	switch (p_variable->property) {
		case VariableNode::PROP_INLINE: {
			FunctionNode *function = alloc_node<FunctionNode>();
			IdentifierNode *identifier = alloc_node<IdentifierNode>();
			complete_extents(identifier);
			identifier->name = "@" + p_variable->identifier->name + "_setter";
			function->identifier = identifier;
			function->is_static = p_variable->is_static;

			consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after "set".)");

			ParameterNode *parameter = alloc_node<ParameterNode>();
			if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected parameter name after "(".)")) {
				reset_extents(parameter, previous);
				p_variable->setter_parameter = parse_identifier();
				parameter->identifier = p_variable->setter_parameter;
				function->parameters_indices[parameter->identifier->name] = 0;
				function->parameters.push_back(parameter);
			}
			complete_extents(parameter);

			consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after parameter name.)*");
			consume(BSTokenizer::Token::COLON, R"*(Expected ":" after ")".)*");

			FunctionNode *previous_function = current_function;
			current_function = function;
			if (p_variable->setter_parameter != nullptr) {
				SuiteNode *body = alloc_node<SuiteNode>();
				body->add_local(parameter, function);
				function->body = parse_suite("setter declaration", body);
				p_variable->setter = function;
			}
			current_function = previous_function;
			complete_extents(function);
			break;
		}
		case VariableNode::PROP_SETGET:
			consume(BSTokenizer::Token::EQUAL, R"(Expected "=" after "set")");
			make_completion_context(COMPLETION_PROPERTY_METHOD, p_variable);
			if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected setter function name after "=".)")) {
				p_variable->setter_pointer = parse_identifier();
			}
			break;
		case VariableNode::PROP_NONE:
			break; // Unreachable.
	}
}

void BSParser::parse_property_getter(VariableNode *p_variable) {
	switch (p_variable->property) {
		case VariableNode::PROP_INLINE: {
			FunctionNode *function = alloc_node<FunctionNode>();

			if (match(BSTokenizer::Token::PARENTHESIS_OPEN)) {
				consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after "get(".)*");
				consume(BSTokenizer::Token::COLON, R"*(Expected ":" after "get()".)*");
			} else {
				consume(BSTokenizer::Token::COLON, R"(Expected ":" or "(" after "get".)");
			}

			IdentifierNode *identifier = alloc_node<IdentifierNode>();
			complete_extents(identifier);
			identifier->name = "@" + p_variable->identifier->name + "_getter";
			function->identifier = identifier;
			function->is_static = p_variable->is_static;

			FunctionNode *previous_function = current_function;
			current_function = function;

			SuiteNode *body = alloc_node<SuiteNode>();
			function->body = parse_suite("getter declaration", body);
			p_variable->getter = function;

			current_function = previous_function;
			complete_extents(function);
			break;
		}
		case VariableNode::PROP_SETGET:
			consume(BSTokenizer::Token::EQUAL, R"(Expected "=" after "get")");
			make_completion_context(COMPLETION_PROPERTY_METHOD, p_variable);
			if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected getter function name after "=".)")) {
				p_variable->getter_pointer = parse_identifier();
			}
			break;
		case VariableNode::PROP_NONE:
			break; // Unreachable.
	}
}

BSParser::ConstantNode *BSParser::parse_constant(const DeclarationModifiers &p_modifiers) {
	ConstantNode *constant = alloc_node<ConstantNode>();

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected constant name after "const".)")) {
		complete_extents(constant);
		return nullptr;
	}

	constant->identifier = parse_identifier();

	if (match(BSTokenizer::Token::COLON)) {
		if (check((BSTokenizer::Token::EQUAL))) {
			// Infer type.
			constant->infer_datatype = true;
		} else {
			// Parse type.
			constant->datatype_specifier = parse_type();
			if (constant->datatype_specifier == nullptr) {
				push_error(R"(Expected type after ":")");
			}
		}
	}

	if (consume(BSTokenizer::Token::EQUAL, R"(Expected initializer after constant name.)")) {
		// Initializer.
		constant->initializer = parse_expression(false);

		if (constant->initializer == nullptr) {
			push_error(R"(Expected initializer expression for constant.)");
			complete_extents(constant);
			return nullptr;
		}
	} else {
		complete_extents(constant);
		return nullptr;
	}

	complete_extents(constant);
	end_statement("constant declaration");

	return constant;
}

BSParser::ParameterNode *BSParser::parse_parameter(bool p_allow_annotations) {
	List<AnnotationNode *> pending_annotations;
	if (p_allow_annotations) {
		while (match(BSTokenizer::Token::ANNOTATION)) {
			AnnotationNode *annotation = parse_annotation(AnnotationInfo::PARAMETER);
			if (annotation != nullptr) {
				pending_annotations.push_back(annotation);
			}
		}
	}

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected parameter name.)")) {
		return nullptr;
	}

	ParameterNode *parameter = alloc_node<ParameterNode>();
	parameter->identifier = parse_identifier();

	if (match(BSTokenizer::Token::COLON)) {
		if (check((BSTokenizer::Token::EQUAL))) {
			// Infer type.
			parameter->infer_datatype = true;
		} else {
			// Parse type.
			make_completion_context(COMPLETION_TYPE_NAME, parameter);
			parameter->datatype_specifier = parse_type();
			if (parameter->datatype_specifier == nullptr) {
				push_error(R"(Expected type after ":")");
			}
		}
	}

	if (match(BSTokenizer::Token::EQUAL)) {
		// Default value.
		parameter->initializer = parse_expression(false);
		if (parameter->initializer == nullptr) {
			push_error(R"(Expected expression for parameter default value after "=".)");
		}
	}

	for (AnnotationNode *&annotation : pending_annotations) {
		parameter->annotations.push_back(annotation);
	}

	complete_extents(parameter);
	return parameter;
}

BSParser::SignalNode *BSParser::parse_signal(const DeclarationModifiers &p_modifiers) {
	SignalNode *signal = alloc_node<SignalNode>();

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected signal name after "signal".)")) {
		complete_extents(signal);
		return nullptr;
	}

	signal->identifier = parse_identifier();

	if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
		push_multiline(true);
		advance();
		do {
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			ParameterNode *parameter = parse_parameter();
			if (parameter == nullptr) {
				push_error("Expected signal parameter name.");
				break;
			}
			if (parameter->initializer != nullptr) {
				push_error(R"(Signal parameters cannot have a default value.)");
			}
			if (signal->parameters_indices.has(parameter->identifier->name)) {
				push_error(vformat(R"(Parameter with name "%s" was already declared for this signal.)", parameter->identifier->name));
			} else {
				signal->parameters_indices[parameter->identifier->name] = signal->parameters.size();
				signal->parameters.push_back(parameter);
			}
		} while (match(BSTokenizer::Token::COMMA) && !is_at_end());

		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after signal parameters.)*");
	}

	complete_extents(signal);
	end_statement("signal declaration");

	return signal;
}

void BSParser::finalize_enum_function(EnumNode *p_enum, FunctionNode *p_function,
		List<AnnotationNode *> &p_annotations, int &r_min_doc_line, bool p_store) {
	if (p_function == nullptr) {
		return;
	}

#ifdef TOOLS_ENABLED
	int doc_comment_line = p_function->start_line - 1;
#endif // TOOLS_ENABLED

	for (AnnotationNode *&annotation : p_annotations) {
		p_function->annotations.push_back(annotation);
#ifdef TOOLS_ENABLED
		if (annotation->start_line <= doc_comment_line) {
			doc_comment_line = annotation->start_line - 1;
		}
#endif // TOOLS_ENABLED
	}

#ifdef TOOLS_ENABLED
	if (has_comment(p_function->start_line, true)) {
		p_function->doc_data = parse_doc_comment(p_function->start_line, true);
	} else if (doc_comment_line >= r_min_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
		p_function->doc_data = parse_doc_comment(doc_comment_line);
	}
	r_min_doc_line = p_function->end_line + 1;
#endif // TOOLS_ENABLED

	p_function->owner_enum = p_enum;
	if (!p_store || p_function->identifier == nullptr) {
		return;
	}

	if (!p_enum->functions_indices.has(p_function->identifier->name)) {
		p_enum->functions_indices[p_function->identifier->name] = p_enum->functions.size();
	}
	p_enum->functions.push_back(p_function);
}

void BSParser::parse_enum_case_payload(EnumNode::Value &r_value) {
	// Enable multiline mode before consuming the open paren, so the tokenizer suppresses
	// NEWLINE/INDENT/DEDENT for the token it scans immediately after it (matching how tuple
	// declaration fields and other parenthesized lists are parsed).
	push_multiline(true);
	advance(); // Consume "(".

	HashMap<StringName, int> field_names;
	if (!check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
		do {
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				break; // Allow for trailing comma.
			}

			EnumNode::PayloadField field;
			field.line = current.start_line;
			field.start_column = current.start_column;

			if (check(BSTokenizer::Token::IDENTIFIER) && peek().type == BSTokenizer::Token::COLON) {
				advance();
				field.identifier = parse_identifier();
				consume(BSTokenizer::Token::COLON, R"(Expected ":" after enum case payload field name.)");
			} else {
				push_error(R"*(Enum case payload fields must be named, e.g. "Move(x: int, y: int)".)*");
			}
			field.type = parse_type(false);

			if (field.type == nullptr) {
				push_error(R"(Expected a field type in enum case payload.)");
				break;
			}
			field.end_column = previous.end_column;

			if (field.identifier != nullptr) {
				if (field_names.has(field.identifier->name)) {
					push_error(vformat(R"(Enum case payload field "%s" was already declared.)", field.identifier->name), field.identifier);
				} else {
					field_names[field.identifier->name] = r_value.payload_fields.size();
				}
			}

			r_value.payload_fields.push_back(field);
		} while (match(BSTokenizer::Token::COMMA));
	}

	if (r_value.payload_fields.is_empty()) {
		push_error(R"(An enum case payload must have at least one field.)");
	}

	r_value.payload_close_line = current.start_line;
	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after enum case payload fields.)*");
}

BSParser::EnumNode *BSParser::parse_enum(const DeclarationModifiers &p_modifiers) {
	EnumNode *enum_node = alloc_node<EnumNode>();
	bool named = false;

	reject_reserved_type_name();
	if (match(BSTokenizer::Token::IDENTIFIER)) {
		enum_node->identifier = parse_identifier();
		named = true;
	}

	if (named) {
		parse_type_parameters(enum_node->type_parameters);
	} else if (check(BSTokenizer::Token::BRACKET_OPEN)) {
		// Consume the list anyway so the rest of the declaration still parses and the failure
		// reports the one real problem instead of cascading into the enum body.
		push_error(R"(Type parameters require a named tagged union.)");
		Vector<TypeParameterNode *> discarded_parameters;
		parse_type_parameters(discarded_parameters);
	}

	consume(BSTokenizer::Token::COLON, vformat(R"(Expected ":" after %s.)", named ? "enum name" : R"("enum")"));
	if (!match(BSTokenizer::Token::NEWLINE)) {
		push_error(vformat(R"(Expected an indented block after %s declaration.)", named ? "enum" : R"("enum")"));
		complete_extents(enum_node);
		return enum_node;
	}
	if (!consume(BSTokenizer::Token::INDENT, R"(Expected indented block after enum declaration.)")) {
		complete_extents(enum_node);
		return enum_node;
	}

	// The enum's own type parameters are in scope for its case payloads and its functions.
	EnumNode *previous_enum = current_enum;
	current_enum = enum_node;
#ifdef TOOLS_ENABLED
	// The INDENT token is anchored to the first body line. Start the doc-comment
	// search after the enum header so a comment immediately above the first value
	// is not mistaken for being inside the indentation marker.
	int min_enum_value_doc_line = enum_node->start_line + 1;
#endif

	HashMap<StringName, int> elements;

#ifdef DEBUG_ENABLED
	// Upstream asks the language singleton for its public functions (fs_parser.cpp:2999-3000 @
	// c9d5e35) to warn when an enum value shadows one. `BaristaScriptLanguage` answers the same
	// question through the extension surface, and answers nothing at all before it is registered.
	const List<MethodInfo> bs_funcs = BaristaScriptLanguage::get_public_function_list();
#endif

	bool saw_value = false;
	bool saw_function = false;
	bool saw_pass = false;
	int min_enum_function_doc_line = enum_node->start_line + 1;
	auto skip_invalid_enum_declaration = [&]() {
		while (!is_at_end() && !check(BSTokenizer::Token::NEWLINE) && !check(BSTokenizer::Token::DEDENT)) {
			advance();
		}
		if (match(BSTokenizer::Token::NEWLINE) && match(BSTokenizer::Token::INDENT)) {
			int indent_depth = 1;
			while (!is_at_end() && indent_depth > 0) {
				if (match(BSTokenizer::Token::INDENT)) {
					indent_depth++;
				} else if (match(BSTokenizer::Token::DEDENT)) {
					indent_depth--;
				} else {
					advance();
				}
			}
		}
	};

	while (!is_at_end() && !check(BSTokenizer::Token::DEDENT)) {
		if (match(BSTokenizer::Token::NEWLINE)) {
			continue;
		}
		if (match(BSTokenizer::Token::ANNOTATION)) {
			AnnotationNode *annotation = parse_annotation(AnnotationInfo::FUNCTION);
			if (annotation != nullptr) {
				annotation_stack.push_back(annotation);
			}
			continue;
		}
		if (match(BSTokenizer::Token::PASS)) {
			if (!annotation_stack.is_empty()) {
				parse_class_member_annotations(AnnotationInfo::NONE, R"("pass")");
			}
			if (saw_value || saw_function || saw_pass) {
				push_error(R"("pass" is only valid for an empty enum.)");
			}
			saw_pass = true;
			end_statement(R"("pass" in enum body)");
			continue;
		}
		if (saw_pass) {
			push_error(R"(An enum containing "pass" cannot contain values or functions.)");
			skip_invalid_enum_declaration();
			continue;
		}

		// `async` is only a genuine function modifier when it leads into another modifier
		// or `func`; a case literally named `async` may be a bare tagged-union case
		// (`async` alone), a value (`async = 1`), or a payload case (`async(value: int)`),
		// none of which start with another modifier token or `func`.
		const bool contextual_async_modifier = current.type == BSTokenizer::Token::IDENTIFIER &&
				current.get_identifier() == StringName("async") &&
				(peek().type == BSTokenizer::Token::FUNC ||
						peek().type == BSTokenizer::Token::STATIC ||
						peek().type == BSTokenizer::Token::ABSTRACT ||
						peek().type == BSTokenizer::Token::FINAL ||
						(peek().type == BSTokenizer::Token::IDENTIFIER && peek().get_identifier() == StringName("async")));
		const bool starts_function_declaration = check(BSTokenizer::Token::FUNC) ||
				check(BSTokenizer::Token::STATIC) ||
				check(BSTokenizer::Token::ABSTRACT) ||
				check(BSTokenizer::Token::FINAL) ||
				contextual_async_modifier;
		if (starts_function_declaration) {
			DeclarationModifiers modifiers = collect_declaration_modifiers();
			if (!check(BSTokenizer::Token::FUNC)) {
				push_error(R"(Only function declarations are allowed in enum bodies.)");
				skip_invalid_enum_declaration();
				continue;
			}

			validate_declaration_modifiers(modifiers, "enum functions", false, true, true, false, false);
			advance();
			List<AnnotationNode *> annotations = parse_class_member_annotations(AnnotationInfo::FUNCTION, "enum function");
			FunctionNode *function = parse_function_declaration(modifiers);
			if (!named) {
				push_error("Only named enums can declare functions.", function);
			}
			finalize_enum_function(enum_node, function, annotations, min_enum_function_doc_line, named);
			saw_function = true;
			continue;
		}

		const bool starts_non_function_declaration = check(BSTokenizer::Token::VAR) ||
				check(BSTokenizer::Token::TK_CONST) ||
				check(BSTokenizer::Token::SIGNAL) ||
				check(BSTokenizer::Token::CLASS) ||
				check(BSTokenizer::Token::TRAIT) ||
				check(BSTokenizer::Token::ENUM) ||
				check(BSTokenizer::Token::NAMESPACE) ||
				check(BSTokenizer::Token::IMPORT) ||
				(current.type == BSTokenizer::Token::IDENTIFIER &&
						(current.get_identifier() == StringName("annotation") ||
								current.get_identifier() == StringName("extend")));
		if (starts_non_function_declaration) {
			if (!annotation_stack.is_empty()) {
				parse_class_member_annotations(AnnotationInfo::NONE, "enum body declaration");
			}
			push_error(R"(Only function declarations are allowed in enum bodies.)");
			skip_invalid_enum_declaration();
			continue;
		}

		if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected identifier for enum key.)")) {
			if (!annotation_stack.is_empty()) {
				parse_class_member_annotations(AnnotationInfo::NONE, "enum value");
			}
			if (saw_function) {
				push_error("Enum values must be declared before enum functions.");
			}
			BSParser::IdentifierNode *identifier = parse_identifier();

			EnumNode::Value item;
			item.identifier = identifier;
			item.parent_enum = enum_node;
			item.line = previous.start_line;
			item.end_line = previous.start_line;
			item.start_column = previous.start_column;
			item.end_column = previous.end_column;

			if (elements.has(item.identifier->name)) {
				push_error(vformat(R"(Name "%s" was already in this enum (at line %d).)", item.identifier->name, elements[item.identifier->name]), item.identifier);
			} else if (!named) {
				if (current_class->members_indices.has(item.identifier->name)) {
					push_error(vformat(R"(Name "%s" is already used as a class %s.)", item.identifier->name, current_class->get_member(item.identifier->name).get_type_name()));
				}
			}

			elements[item.identifier->name] = item.line;

			if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
				parse_enum_case_payload(item);
				item.end_line = item.payload_close_line;
				item.end_column = previous.end_column;
				if (item.has_payload()) {
					enum_node->is_tagged_union = true;
				}
			}

			if (check(BSTokenizer::Token::EQUAL)) {
				advance();
				ExpressionNode *value = parse_expression(false);
				if (value == nullptr) {
					push_error(R"(Expected expression value after "=".)");
				}
				item.custom_value = value;
			}
			// Whether a missing "=" is an error depends on whether the enum ends up a tagged
			// union, which is only known once the whole body has been parsed (a later case may
			// still introduce a payload). Validated in a second pass below.

			item.index = enum_node->values.size();
			enum_node->values.push_back(item);
			if (!named) {
				// Add as member of current class.
				current_class->add_member(item);
			}
			saw_value = true;
		} else {
			// Avoid getting stuck after a malformed member and keep the diagnostic
			// anchored to the enum body rather than cascading into the outer class.
			push_error(R"(Expected enum key, function declaration, or "pass" in enum body.)");
			advance();
		}

		if (check(BSTokenizer::Token::COMMA)) {
			push_error(R"(Enum values must be separated by newlines; commas are not allowed between enum members.)");
			advance();
		}
		end_statement("enum value");
	}
	if (!annotation_stack.is_empty()) {
		parse_class_member_annotations(AnnotationInfo::NONE, "enum body");
	}

	// Whether the enum is a tagged union is only known once the whole body has been parsed (a
	// later case may still introduce a payload), so explicit-value validation for every case is
	// deferred to this single second pass instead of running inline in the loop above.
	for (const EnumNode::Value &value : enum_node->values) {
		if (enum_node->is_tagged_union) {
			// Tags are ordinal by declaration order in a tagged union; explicit values would let
			// them drift, so they are rejected for every case, payload-bearing or not.
			if (value.custom_value != nullptr) {
				push_error(R"(Explicit values are not allowed in a tagged union; case tags are ordinal by declaration order.)", value.custom_value);
			}
		} else if (value.custom_value == nullptr) {
			push_error(R"(Expected "=" and an integer value after enum key.)", value.identifier);
		}
	}

	// Only tagged unions can be generic: an integer-backed enum has no payload for a type
	// parameter to appear in. Like the explicit-value check above, this can only be decided once
	// the whole body is known, because a later case may still introduce a payload.
	if (!enum_node->type_parameters.is_empty() && !enum_node->is_tagged_union) {
		push_error(vformat(R"(Generic enum "%s" must contain at least one payload-bearing case.)",
						   String(enum_node->identifier->name)),
				enum_node->identifier);
	}

#ifdef TOOLS_ENABLED
	// Enum values documentation.
	for (int i = 0; i < enum_node->values.size(); i++) {
		int enum_value_line = enum_node->values[i].line;
		int doc_comment_line = enum_value_line - 1;

		MemberDocData doc_data;
		if (has_comment(enum_value_line, true)) {
			// Inline doc comment.
			if (i == enum_node->values.size() - 1 || enum_node->values[i + 1].line > enum_value_line) {
				doc_data = parse_doc_comment(enum_value_line, true);
			}
		} else if (doc_comment_line >= min_enum_value_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment.
			doc_data = parse_doc_comment(doc_comment_line);
		}

		if (named) {
			enum_node->values.write[i].doc_data = doc_data;
		} else {
			current_class->set_enum_value_doc_data(enum_node->values[i].identifier->name, doc_data);
		}

		min_enum_value_doc_line = enum_value_line + 1; // Prevent multiple enum values from using the same doc comment.
	}
#endif // TOOLS_ENABLED

	consume(BSTokenizer::Token::DEDENT, R"(Missing unindent at the end of the enum body.)");
	complete_extents(enum_node);

	current_enum = previous_enum;
	return enum_node;
}

BSParser::TupleNode *BSParser::parse_tuple(const DeclarationModifiers &p_modifiers) {
	TupleNode *tuple_node = alloc_node<TupleNode>();

	reject_reserved_type_name();
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected tuple name after "tuple".)")) {
		complete_extents(tuple_node);
		return tuple_node;
	}
	tuple_node->identifier = parse_identifier();

	if (!check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
		push_error(R"(Expected "(" after tuple name.)");
		complete_extents(tuple_node);
		return tuple_node;
	}
	// Enable multiline mode before consuming the open paren, so the tokenizer suppresses
	// NEWLINE/INDENT/DEDENT for the token it scans immediately after it (matching how the
	// expression-level Pratt driver handles grouping/call parentheses).
	push_multiline(true);
	advance();

	HashMap<StringName, int> field_names;
	if (!check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
		do {
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				break; // Allow for trailing comma.
			}

			TupleNode::Field field;
			field.line = current.start_line;
			field.start_column = current.start_column;

			// A field is either `name: Type` (named) or a bare `Type` (positional).
			if (check(BSTokenizer::Token::IDENTIFIER) && peek().type == BSTokenizer::Token::COLON) {
				advance();
				field.identifier = parse_identifier();
				consume(BSTokenizer::Token::COLON, R"(Expected ":" after tuple field name.)");
			}
			field.type = parse_type(false);

			if (field.type == nullptr) {
				push_error(R"(Expected a field type in tuple declaration.)");
				break;
			}
			field.end_column = previous.end_column;

			if (field.identifier != nullptr) {
				if (field_names.has(field.identifier->name)) {
					push_error(vformat(R"(Tuple field "%s" was already declared.)", field.identifier->name), field.identifier);
				} else {
					field_names[field.identifier->name] = tuple_node->fields.size();
				}
			}

			tuple_node->fields.push_back(field);
		} while (match(BSTokenizer::Token::COMMA));
	}

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after tuple fields.)*");

	if (tuple_node->fields.size() < 2) {
		push_error(R"(A tuple declaration must have at least two fields.)", tuple_node);
	}

	complete_extents(tuple_node);
	end_statement("tuple declaration");
	return tuple_node;
}

bool BSParser::parse_function_signature(FunctionNode *p_function, SuiteNode *p_body, const String &p_type, int p_signature_start) {
	if (!check(BSTokenizer::Token::PARENTHESIS_CLOSE) && !is_at_end()) {
		bool default_used = false;
		do {
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			bool is_rest = false;
			if (match(BSTokenizer::Token::PERIOD_PERIOD_PERIOD)) {
				is_rest = true;
			}

			ParameterNode *parameter = parse_parameter();
			if (parameter == nullptr) {
				break;
			}

			if (p_function->is_vararg()) {
				push_error("Cannot have parameters after the rest parameter.");
				continue;
			}

			if (parameter->initializer != nullptr) {
				if (is_rest) {
					push_error("The rest parameter cannot have a default value.");
					continue;
				}
				default_used = true;
			} else {
				if (default_used && !is_rest) {
					push_error("Cannot have mandatory parameters after optional parameters.");
					continue;
				}
			}

			if (p_function->parameters_indices.has(parameter->identifier->name)) {
				push_error(vformat(R"(Parameter with name "%s" was already declared for this %s.)", parameter->identifier->name, p_type));
			} else if (is_rest) {
				p_function->rest_parameter = parameter;
				p_body->add_local(parameter, current_function);
			} else {
				p_function->parameters_indices[parameter->identifier->name] = p_function->parameters.size();
				p_function->parameters.push_back(parameter);
				p_body->add_local(parameter, current_function);
			}
		} while (match(BSTokenizer::Token::COMMA));
	}

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, vformat(R"*(Expected closing ")" after %s parameters.)*", p_type));

	if (match(BSTokenizer::Token::FORWARD_ARROW)) {
		make_completion_context(COMPLETION_TYPE_NAME_OR_VOID, p_function);
		p_function->return_type = parse_type(true);
		if (p_function->return_type == nullptr) {
			push_error(R"(Expected return type or "void" after "->".)");
		}
	}

	if (!p_function->source_lambda && p_function->identifier && p_function->identifier->name == BaristaScriptLanguage::get_interned_strings()._static_init) {
		if (!p_function->is_static) {
			push_error(R"(Static constructor must be declared static.)");
		}
		if (!p_function->parameters.is_empty() || p_function->is_vararg()) {
			push_error(R"(Static constructor cannot have parameters.)");
		}
		current_class->has_static_data = true;
	}

#ifdef TOOLS_ENABLED
	if (p_type == "function" && p_signature_start != -1) {
		const int signature_end_pos = tokenizer->get_current_position() - 1;
		const String source_code = tokenizer->get_source_code();
		p_function->signature = source_code.substr(p_signature_start, signature_end_pos - p_signature_start).strip_edges(false, true);
	}
#endif // TOOLS_ENABLED

	// TODO: Improve token consumption so it synchronizes to a statement boundary. This way we can get into the function body with unrecognized tokens.
	if (p_type == "lambda") {
		const bool ok = consume(BSTokenizer::Token::COLON, R"(Expected ":" after lambda declaration.)");
		p_function->signature_end_line = previous.end_line;
		return ok;
	}
	// The colon may not be present in the case of abstract functions.
	const bool has_body = match(BSTokenizer::Token::COLON);
	p_function->signature_end_line = previous.end_line;
	return has_body;
}

BSParser::FunctionNode *BSParser::parse_function_declaration(const DeclarationModifiers &p_modifiers) {
	FunctionNode *function = alloc_node<FunctionNode>();
	function->is_static = p_modifiers.is_static;
	function->is_abstract = p_modifiers.is_abstract;
	function->is_final = p_modifiers.is_final;
	function->is_declared_async = p_modifiers.is_async;
	// Declared async functions are coroutine-callable even before parsing a body-level await.
	function->is_coroutine = p_modifiers.is_async;

	make_completion_context(COMPLETION_OVERRIDE_METHOD, function);

#ifdef TOOLS_ENABLED
	// The signature is something like `(a: int, b: int = 0) -> void`.
	// We start one token earlier, since the parser looks one token ahead.
	const int signature_start_pos = tokenizer->get_current_position();
#endif // TOOLS_ENABLED

	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected function name after "func".)")) {
		complete_extents(function);
		return nullptr;
	}

	FunctionNode *previous_function = current_function;
	current_function = function;

	function->identifier = parse_identifier();
	if (function->identifier != nullptr && function->identifier->name == SNAME("async") && check(BSTokenizer::Token::IDENTIFIER)) {
		push_error(R"("async" must appear before "func" when used as a function modifier.)", function->identifier);
		advance();
	}

	parse_type_parameters(function->type_parameters);

	SuiteNode *body = alloc_node<SuiteNode>();

	SuiteNode *previous_suite = current_suite;
	current_suite = body;

	push_multiline(true);
	consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected opening "(" after function name.)");

#ifdef TOOLS_ENABLED
	const bool has_body = parse_function_signature(function, body, "function", signature_start_pos);
#else // !TOOLS_ENABLED
	const bool has_body = parse_function_signature(function, body, "function", -1);
#endif // TOOLS_ENABLED

	current_suite = previous_suite;

#ifdef TOOLS_ENABLED
	function->min_local_doc_line = previous.end_line + 1;
#endif // TOOLS_ENABLED

	function->has_body = has_body;

	if (!has_body) {
		// Abstract functions do not have a body.
		end_statement("bodyless function declaration");
		reset_extents(body, current);
		complete_extents(body);
		function->body = body;
	} else {
		function->body = parse_suite("function declaration", body);
	}

	current_function = previous_function;
	complete_extents(function);
	return function;
}

BSParser::AnnotationNode *BSParser::parse_annotation(uint32_t p_valid_targets) {
	AnnotationNode *annotation = alloc_node<AnnotationNode>();

	annotation->name = previous.literal;

	make_completion_context(COMPLETION_ANNOTATION, annotation);

	bool valid = true;

	if (!valid_annotations.has(annotation->name)) {
		if (annotation->name == "@deprecated") {
			push_error(R"("@deprecated" annotation does not exist. Use "## @deprecated: Reason here." instead.)");
			valid = false;
		} else if (annotation->name == "@experimental") {
			push_error(R"("@experimental" annotation does not exist. Use "## @experimental: Reason here." instead.)");
			valid = false;
		} else if (annotation->name == "@tutorial") {
			push_error(R"("@tutorial" annotation does not exist. Use "## @tutorial(Title): https://example.com" instead.)");
			valid = false;
		} else {
			// Unknown non-built-in annotation. Preserve it as an unresolved custom usage so the
			// analyzer can resolve it against same-namespace or imported annotation declarations.
			// Its `info` stays null and its target/arguments are validated later by the analyzer.
			annotation->is_custom = true;
		}
	}

	if (valid && !annotation->is_custom) {
		annotation->info = &valid_annotations[annotation->name];

		if (!annotation->applies_to(p_valid_targets)) {
			if (annotation->applies_to(AnnotationInfo::SCRIPT)) {
				push_error(vformat(R"(Annotation "%s" must be at the top of the script, before "extends" and "class_name".)", annotation->name));
			} else {
				push_error(vformat(R"(Annotation "%s" is not allowed in this level.)", annotation->name));
			}
			valid = false;
		}
	}

	if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
		push_multiline(true);
		advance();
		// Arguments.
		push_completion_call(annotation);
		int argument_index = 0;
		do {
			make_completion_context(COMPLETION_ANNOTATION_ARGUMENTS, annotation, argument_index);
			set_last_completion_call_arg(argument_index);
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			ExpressionNode *argument = nullptr;
			StringName argument_name;
			const bool accepts_named_arguments = annotation->is_custom || annotation->name == SNAME("@autoload");
			if (accepts_named_arguments) {
				// Custom annotation usages and the built-in `@autoload` annotation accept
				// `name = value` named arguments. Stop on a trailing "=" so a leading
				// identifier can be read as the argument name.
				// BaristaScript assignment is a statement, never an expression, so `IDENTIFIER` +
				// `EQUAL` here is unambiguously a named argument; `@a(x == y)` uses `EQUAL_EQUAL`.
				argument = parse_expression(false, true);
				if (argument != nullptr && check(BSTokenizer::Token::EQUAL)) {
					if (argument->type == Node::IDENTIFIER) {
						argument_name = static_cast<IdentifierNode *>(argument)->name;
						advance(); // Consume "=".
						make_completion_context(COMPLETION_ANNOTATION_ARGUMENTS, annotation, argument_index);
						argument = parse_expression(false);
						if (argument == nullptr) {
							push_error(vformat(R"(Expected expression after "%s =" named argument.)", argument_name));
						}
					} else {
						// A non-identifier target before "=" is an attempted assignment, which is
						// not a valid expression argument. Consume the rest so parsing recovers.
						push_error(R"(Assignment is not allowed inside an expression.)");
						advance(); // Consume "=".
						parse_expression(false);
					}
				}
			} else {
				argument = parse_expression(false);
			}

			if (argument == nullptr) {
				if (argument_name == StringName()) {
					push_error("Expected expression as the annotation argument.");
					valid = false;
				}
			} else {
				annotation->arguments.push_back(argument);
				annotation->argument_names.push_back(argument_name);

				if (argument->type == Node::LITERAL) {
					override_completion_context(argument, COMPLETION_ANNOTATION_ARGUMENTS, annotation, argument_index);
				}
			}

			argument_index++;
		} while (match(BSTokenizer::Token::COMMA));

		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after annotation arguments.)*");
		pop_completion_call();
	}
	complete_extents(annotation);

	match(BSTokenizer::Token::NEWLINE); // Newline after annotation is optional.

	if (valid && !annotation->is_custom) {
		valid = validate_annotation_arguments(annotation);
	}

	return valid ? annotation : nullptr;
}

void BSParser::clear_unused_annotations() {
	for (const AnnotationNode *annotation : annotation_stack) {
		push_error(vformat(R"(Annotation "%s" does not precede a valid target, so it will have no effect.)", annotation->name), annotation);
	}

	annotation_stack.clear();
}

bool BSParser::register_annotation(const MethodInfo &p_info, uint32_t p_target_kinds, AnnotationAction p_apply, const Vector<Variant> &p_default_arguments, bool p_is_vararg) {
	ERR_FAIL_COND_V_MSG(valid_annotations.has(p_info.name), false, vformat(R"(Annotation "%s" already registered.)", p_info.name));

	AnnotationInfo new_annotation;
	new_annotation.info = p_info;
	new_annotation.info.default_arguments = p_default_arguments;
	if (p_is_vararg) {
		new_annotation.info.flags |= METHOD_FLAG_VARARG;
	}
	new_annotation.apply = p_apply;
	new_annotation.target_kind = p_target_kinds;

	valid_annotations[p_info.name] = new_annotation;
	return true;
}

BSParser::SuiteNode *BSParser::parse_suite(const String &p_context, SuiteNode *p_suite, bool p_for_lambda) {
	SuiteNode *suite = p_suite != nullptr ? p_suite : alloc_node<SuiteNode>();
	suite->parent_block = current_suite;
	suite->parent_function = current_function;
	current_suite = suite;

	if (!p_for_lambda && suite->parent_block != nullptr && suite->parent_block->is_in_loop) {
		// Do not reset to false if true is set before calling parse_suite().
		suite->is_in_loop = true;
	}

	bool multiline = false;

	if (match(BSTokenizer::Token::NEWLINE)) {
		multiline = true;
	}

	if (multiline) {
		if (!consume(BSTokenizer::Token::INDENT, vformat(R"(Expected indented block after %s.)", p_context))) {
			current_suite = suite->parent_block;
			complete_extents(suite);
			return suite;
		}
	}
	reset_extents(suite, current);

	int error_count = 0;

	do {
		if (is_at_end() || (!multiline && previous.type == BSTokenizer::Token::SEMICOLON && check(BSTokenizer::Token::NEWLINE))) {
			break;
		}
		Node *statement = parse_statement();
		if (statement == nullptr) {
			if (error_count++ > 100) {
				push_error("Too many statement errors.", suite);
				break;
			}
			continue;
		}
		suite->statements.push_back(statement);

		// Register locals.
		switch (statement->type) {
			case Node::VARIABLE: {
				VariableNode *variable = static_cast<VariableNode *>(statement);
				const SuiteNode::Local &local = current_suite->get_local(variable->identifier->name);
				if (local.type != SuiteNode::Local::UNDEFINED) {
					push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", local.get_name(), variable->identifier->name), variable->identifier);
				}
				current_suite->add_local(variable, current_function);
				break;
			}
			case Node::VARIABLE_DESTRUCTURE: {
				VariableDestructureNode *destructure = static_cast<VariableDestructureNode *>(statement);
				for (int i = 0; i < destructure->bindings.size(); i++) {
					VariableNode *binding = destructure->bindings[i];
					if (binding == nullptr) {
						continue; // A `_` slot binds nothing.
					}
					const SuiteNode::Local &local = current_suite->get_local(binding->identifier->name);
					if (local.type != SuiteNode::Local::UNDEFINED) {
						push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", local.get_name(), binding->identifier->name), binding->identifier);
					}
					current_suite->add_local(binding, current_function);
				}
				break;
			}
			case Node::CONSTANT: {
				ConstantNode *constant = static_cast<ConstantNode *>(statement);
				const SuiteNode::Local &local = current_suite->get_local(constant->identifier->name);
				if (local.type != SuiteNode::Local::UNDEFINED) {
					String name;
					if (local.type == SuiteNode::Local::CONSTANT) {
						name = "constant";
					} else {
						name = "variable";
					}
					push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", name, constant->identifier->name), constant->identifier);
				}
				current_suite->add_local(constant, current_function);
				break;
			}
			default:
				break;
		}

	} while ((multiline || previous.type == BSTokenizer::Token::SEMICOLON) && !check(BSTokenizer::Token::DEDENT) && !lambda_ended && !is_at_end());

	complete_extents(suite);

	if (multiline) {
		if (!lambda_ended) {
			consume(BSTokenizer::Token::DEDENT, vformat(R"(Missing unindent at the end of %s.)", p_context));

		} else {
			match(BSTokenizer::Token::DEDENT);
		}
	} else if (previous.type == BSTokenizer::Token::SEMICOLON) {
		consume(BSTokenizer::Token::NEWLINE, vformat(R"(Expected newline after ";" at the end of %s.)", p_context));
	}

	if (p_for_lambda) {
		lambda_ended = true;
	}
	current_suite = suite->parent_block;
	return suite;
}

BSParser::Node *BSParser::parse_statement() {
	// Compound statements recurse through parse_suite() back into parse_statement();
	// bound that depth so deeply nested blocks (e.g. chained single-line `if`s) report
	// an error instead of overflowing the native stack.
	RecursionDepthGuard depth_guard(statement_nesting_depth);
	if (unlikely(statement_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Statement nesting is too deep.");
		return nullptr;
	}

	Node *result = nullptr;
#ifdef DEBUG_ENABLED
	bool unreachable = current_suite->has_return && !current_suite->has_unreachable_code;
#endif

	List<AnnotationNode *> annotations;
	if (current.type != BSTokenizer::Token::ANNOTATION) {
		while (!annotation_stack.is_empty()) {
			AnnotationNode *last_annotation = annotation_stack.back()->get();
			if (last_annotation->applies_to(AnnotationInfo::STATEMENT)) {
				annotations.push_front(last_annotation);
				annotation_stack.pop_back();
			} else {
				push_error(vformat(R"(Annotation "%s" cannot be applied to a statement.)", last_annotation->name));
				clear_unused_annotations();
			}
		}
	}

	switch (current.type) {
		case BSTokenizer::Token::PASS:
			advance();
			result = alloc_node<PassNode>();
			complete_extents(result);
			end_statement(R"("pass")");
			break;
		case BSTokenizer::Token::VAR:
			advance();
			if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
				result = parse_variable_destructure(false);
			} else {
				result = parse_variable(false, false);
			}
			break;
		case BSTokenizer::Token::FINAL:
			advance();
			if (match(BSTokenizer::Token::VAR)) {
				result = parse_variable(false, false, true);
			} else if (check(BSTokenizer::Token::TK_CONST)) {
				advance();
				push_error(R"(The "final" modifier cannot be applied to constants.)");
				result = parse_constant(DeclarationModifiers());
			} else {
				push_error(R"(Expected "var" after "final".)");
			}
			break;
		case BSTokenizer::Token::TK_CONST:
			advance();
			if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
				result = parse_variable_destructure(true);
			} else {
				result = parse_constant(DeclarationModifiers());
			}
			break;
		case BSTokenizer::Token::IF:
			advance();
			result = parse_if();
			break;
		case BSTokenizer::Token::FOR:
			advance();
			result = parse_for();
			break;
		case BSTokenizer::Token::WHILE:
			advance();
			result = parse_while();
			break;
		case BSTokenizer::Token::MATCH:
			advance();
			result = parse_match();
			break;
		case BSTokenizer::Token::BREAK:
			advance();
			result = parse_break();
			break;
		case BSTokenizer::Token::CONTINUE:
			advance();
			result = parse_continue();
			break;
		case BSTokenizer::Token::RETURN: {
			advance();
			ReturnNode *n_return = alloc_node<ReturnNode>();
			if (!is_statement_end()) {
				if (current_function && (current_function->identifier->name == BaristaScriptLanguage::get_interned_strings()._init || current_function->identifier->name == BaristaScriptLanguage::get_interned_strings()._static_init)) {
					push_error(R"(Constructor cannot return a value.)");
				}
				n_return->return_value = parse_expression(false);
			} else if (in_lambda && !is_statement_end_token()) {
				// Try to parse it anyway as this might not be the statement end in a lambda.
				// If this fails the expression will be nullptr, but that's the same as no return, so it's fine.
				n_return->return_value = parse_expression(false);
			}
			complete_extents(n_return);
			result = n_return;

			current_suite->has_return = true;

			end_statement("return statement");
			break;
		}
		case BSTokenizer::Token::BREAKPOINT:
			advance();
			result = alloc_node<BreakpointNode>();
			complete_extents(result);
			end_statement(R"("breakpoint")");
			break;
		case BSTokenizer::Token::ASSERT:
			advance();
			result = parse_assert();
			break;
		case BSTokenizer::Token::ANNOTATION: {
			advance();
			AnnotationNode *annotation = parse_annotation(AnnotationInfo::STATEMENT | AnnotationInfo::STANDALONE);
			if (annotation != nullptr) {
				if (annotation->applies_to(AnnotationInfo::STANDALONE)) {
					if (previous.type != BSTokenizer::Token::NEWLINE) {
						push_error(R"(Expected newline after a standalone annotation.)");
					}
					if (annotation->name == SNAME("@warning_ignore_start") || annotation->name == SNAME("@warning_ignore_restore")) {
						// Some annotations need to be resolved and applied in the parser.
						annotation->apply(this, nullptr, nullptr);
					} else {
						push_error(R"(Unexpected standalone annotation.)");
					}
				} else {
					annotation_stack.push_back(annotation);
				}
			}
			break;
		}
		default: {
			if (starts_type_alias_declaration()) {
				// `type` is contextual, so this sequence is unambiguously an alias declaration even
				// though a function body is not where one may be declared. Report the position and
				// consume the declaration so the rest of the body still parses.
				push_error(R"(Type alias declarations are only allowed at file scope or in a class body.)");
				parse_type_alias();
				break;
			}
			// Expression statement.
			ExpressionNode *expression = parse_expression(true); // Allow assignment here.
			bool has_ended_lambda = false;
			if (expression == nullptr) {
				if (in_lambda) {
					// If it's not a valid expression beginning, it might be the continuation of the outer expression where this lambda is.
					lambda_ended = true;
					has_ended_lambda = true;
				} else {
					advance();
					push_error(vformat(R"(Expected statement, found "%s" instead.)", previous.get_name()));
				}
			} else {
				end_statement("expression");
			}
			lambda_ended = lambda_ended || has_ended_lambda;
			result = expression;

#ifdef DEBUG_ENABLED
			if (expression != nullptr) {
				switch (expression->type) {
					case Node::ASSIGNMENT:
					case Node::AWAIT:
					case Node::CALL:
						// Fine.
						break;
					case Node::PRELOAD:
						// `preload` is a function-like keyword.
						push_warning(expression, BSWarning::RETURN_VALUE_DISCARDED, "preload");
						break;
					case Node::LAMBDA:
						// Standalone lambdas can't be used, so make this an error.
						push_error("Standalone lambdas cannot be accessed. Consider assigning it to a variable.", expression);
						break;
					case Node::LITERAL:
						// Allow strings as multiline comments.
						if (static_cast<BSParser::LiteralNode *>(expression)->value.get_type() != Variant::STRING) {
							push_warning(expression, BSWarning::STANDALONE_EXPRESSION);
						}
						break;
					case Node::TERNARY_OPERATOR:
						push_warning(expression, BSWarning::STANDALONE_TERNARY);
						break;
					default:
						push_warning(expression, BSWarning::STANDALONE_EXPRESSION);
				}
			}
#endif
			break;
		}
	}

#ifdef TOOLS_ENABLED
	int doc_comment_line = 0;
	if (result != nullptr) {
		doc_comment_line = result->start_line - 1;
	}
#endif // TOOLS_ENABLED

	if (result != nullptr && !annotations.is_empty()) {
		for (AnnotationNode *&annotation : annotations) {
			result->annotations.push_back(annotation);
#ifdef TOOLS_ENABLED
			if (annotation->start_line <= doc_comment_line) {
				doc_comment_line = annotation->start_line - 1;
			}
#endif // TOOLS_ENABLED
		}
	}

#ifdef TOOLS_ENABLED
	if (result != nullptr) {
		MemberDocData doc_data;
		if (has_comment(result->start_line, true)) {
			// Inline doc comment.
			doc_data = parse_doc_comment(result->start_line, true);
		} else if (doc_comment_line >= current_function->min_local_doc_line && has_comment(doc_comment_line, true) && tokenizer->get_comments()[doc_comment_line].new_line) {
			// Normal doc comment.
			doc_data = parse_doc_comment(doc_comment_line);
		}

		if (result->type == Node::CONSTANT) {
			static_cast<ConstantNode *>(result)->doc_data = doc_data;
		} else if (result->type == Node::VARIABLE) {
			static_cast<VariableNode *>(result)->doc_data = doc_data;
		}

		current_function->min_local_doc_line = result->end_line + 1; // Prevent multiple locals from using the same doc comment.
	}
#endif // TOOLS_ENABLED

#ifdef DEBUG_ENABLED
	if (unreachable && result != nullptr) {
		current_suite->has_unreachable_code = true;
		if (current_function) {
			push_warning(result, BSWarning::UNREACHABLE_CODE, current_function->identifier ? current_function->identifier->name : "<anonymous lambda>");
		}
	}
#endif

	if (panic_mode) {
		synchronize();
	}

	return result;
}

BSParser::AssertNode *BSParser::parse_assert() {
	// TODO: Add assert message.
	AssertNode *assert = alloc_node<AssertNode>();

	push_multiline(true);
	consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after "assert".)");

	int case_binds_mark = pending_case_binds.size();
	{
		RecursionDepthGuard case_bind_condition_guard(case_bind_condition_depth);
		assert->condition = parse_expression(false);
	}
	if (assert->condition == nullptr) {
		push_error("Expected expression to assert.");
		// Unwind any transient case-bind locals declared while parsing the (failed) condition so they
		// don't leak into the rest of the suite as declared locals.
		declare_condition_case_binds(Vector<TypeTestNode *>(), current_suite, case_binds_mark);
		pop_multiline();
		complete_extents(assert);
		return nullptr;
	}

	// An assert has no guarded suite, so its binds live in the enclosing suite from here on, exactly
	// like a variable declaration would.
	Vector<TypeTestNode *> case_bind_tests;
	collect_condition_case_binds(assert->condition, case_bind_tests);
	assert->condition_has_case_binds = declare_condition_case_binds(case_bind_tests, current_suite, case_binds_mark);

	if (match(BSTokenizer::Token::COMMA) && !check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
		assert->message = parse_expression(false);
		if (assert->message == nullptr) {
			push_error(R"(Expected error message for assert after ",".)");
			pop_multiline();
			complete_extents(assert);
			return nullptr;
		}
		match(BSTokenizer::Token::COMMA);
	}

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after assert expression.)*");

	complete_extents(assert);
	end_statement(R"("assert")");

	return assert;
}

BSParser::BreakNode *BSParser::parse_break() {
	if (!can_break) {
		push_error(R"(Cannot use "break" outside of a loop.)");
	}
	BreakNode *break_node = alloc_node<BreakNode>();
	complete_extents(break_node);
	end_statement(R"("break")");
	return break_node;
}

BSParser::ContinueNode *BSParser::parse_continue() {
	if (!can_continue) {
		push_error(R"(Cannot use "continue" outside of a loop.)");
	}
	current_suite->has_continue = true;
	ContinueNode *cont = alloc_node<ContinueNode>();
	complete_extents(cont);
	end_statement(R"("continue")");
	return cont;
}

BSParser::ForNode *BSParser::parse_for() {
	ForNode *n_for = alloc_node<ForNode>();

	if (consume(BSTokenizer::Token::IDENTIFIER, R"(Expected loop variable name after "for".)")) {
		n_for->variable = parse_identifier();
	}

	if (match(BSTokenizer::Token::COLON)) {
		n_for->datatype_specifier = parse_type();
		if (n_for->datatype_specifier == nullptr) {
			push_error(R"(Expected type specifier after ":".)");
		}
	}

	if (n_for->datatype_specifier == nullptr) {
		consume(BSTokenizer::Token::TK_IN, R"(Expected "in" or ":" after "for" variable name.)");
	} else {
		consume(BSTokenizer::Token::TK_IN, R"(Expected "in" after "for" variable type specifier.)");
	}

	n_for->list = parse_expression(false);

	if (!n_for->list) {
		push_error(R"(Expected iterable after "in".)");
	}

	consume(BSTokenizer::Token::COLON, R"(Expected ":" after "for" condition.)");

	// Save break/continue state.
	bool could_break = can_break;
	bool could_continue = can_continue;

	// Allow break/continue.
	can_break = true;
	can_continue = true;

	SuiteNode *suite = alloc_node<SuiteNode>();
	if (n_for->variable) {
		const SuiteNode::Local &local = current_suite->get_local(n_for->variable->name);
		if (local.type != SuiteNode::Local::UNDEFINED) {
			push_error(vformat(R"(There is already a %s named "%s" declared in this scope.)", local.get_name(), n_for->variable->name), n_for->variable);
		}
		suite->add_local(SuiteNode::Local(n_for->variable, current_function));
	}
	suite->is_in_loop = true;
	n_for->loop = parse_suite(R"("for" block)", suite);
	complete_extents(n_for);

	// Reset break/continue state.
	can_break = could_break;
	can_continue = could_continue;

	return n_for;
}

BSParser::IfNode *BSParser::parse_if(const String &p_token) {
	// `elif` chains recurse directly through parse_if() (bypassing parse_statement()), so
	// bound them with the statement depth counter to avoid overflowing the native stack.
	RecursionDepthGuard depth_guard(statement_nesting_depth);
	if (unlikely(statement_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Statement nesting is too deep.");
		return nullptr;
	}

	IfNode *n_if = alloc_node<IfNode>();

	int case_binds_mark = pending_case_binds.size();
	{
		RecursionDepthGuard case_bind_condition_guard(case_bind_condition_depth);
		n_if->condition = parse_expression(false);
	}
	if (n_if->condition == nullptr) {
		push_error(vformat(R"(Expected conditional expression after "%s".)", p_token));
	}

	Vector<TypeTestNode *> case_bind_tests;
	collect_condition_case_binds(n_if->condition, case_bind_tests);

	consume(BSTokenizer::Token::COLON, vformat(R"(Expected ":" after "%s" condition.)", p_token));

	SuiteNode *true_suite = alloc_node<SuiteNode>();
	n_if->condition_has_case_binds = declare_condition_case_binds(case_bind_tests, true_suite, case_binds_mark);
	n_if->true_block = parse_suite(vformat(R"("%s" block)", p_token), true_suite);
	n_if->true_block->parent_if = n_if;

	if (n_if->true_block->has_continue) {
		current_suite->has_continue = true;
	}

	if (match(BSTokenizer::Token::ELIF)) {
		SuiteNode *else_block = alloc_node<SuiteNode>();
		else_block->parent_function = current_function;
		else_block->parent_block = current_suite;

		SuiteNode *previous_suite = current_suite;
		current_suite = else_block;

		IfNode *elif = parse_if("elif");
		if (elif != nullptr) {
			else_block->statements.push_back(elif);
		}
		complete_extents(else_block);
		n_if->false_block = else_block;

		current_suite = previous_suite;
	} else if (match(BSTokenizer::Token::ELSE)) {
		consume(BSTokenizer::Token::COLON, R"(Expected ":" after "else".)");
		n_if->false_block = parse_suite(R"("else" block)");
	}
	complete_extents(n_if);

	if (n_if->false_block != nullptr && n_if->false_block->has_return && n_if->true_block->has_return) {
		current_suite->has_return = true;
	}
	if (n_if->false_block != nullptr && n_if->false_block->has_continue) {
		current_suite->has_continue = true;
	}

	return n_if;
}

BSParser::MatchNode *BSParser::parse_match() {
	MatchNode *match_node = alloc_node<MatchNode>();

	match_node->test = parse_expression(false);
	if (match_node->test == nullptr) {
		push_error(R"(Expected expression to test after "match".)");
	}

	consume(BSTokenizer::Token::COLON, R"(Expected ":" after "match" expression.)");
	consume(BSTokenizer::Token::NEWLINE, R"(Expected a newline after "match" statement.)");

	if (!consume(BSTokenizer::Token::INDENT, R"(Expected an indented block after "match" statement.)")) {
		complete_extents(match_node);
		return match_node;
	}

	bool all_have_return = true;
	bool have_wildcard = false;

	List<AnnotationNode *> match_branch_annotation_stack;

	while (!check(BSTokenizer::Token::DEDENT) && !is_at_end()) {
		if (match(BSTokenizer::Token::PASS)) {
#ifdef TOOLS_ENABLED
			match_node->erased_pass_lines.push_back(previous.start_line);
#endif // TOOLS_ENABLED
			consume(BSTokenizer::Token::NEWLINE, R"(Expected newline after "pass".)");
			continue;
		}

		if (match(BSTokenizer::Token::ANNOTATION)) {
			AnnotationNode *annotation = parse_annotation(AnnotationInfo::STATEMENT);
			if (annotation == nullptr) {
				continue;
			}
			if (annotation->name != SNAME("@warning_ignore")) {
				push_error(vformat(R"(Annotation "%s" is not allowed in this level.)", annotation->name), annotation);
				continue;
			}
			match_branch_annotation_stack.push_back(annotation);
			continue;
		}

		MatchBranchNode *branch = parse_match_branch();
		if (branch == nullptr) {
			advance();
			continue;
		}

		// The innermost match wins: a nested one has already claimed the context by the time its
		// enclosing branch returns here.
		if (completion_context.match == nullptr && completion_context.match_branch == branch) {
			completion_context.match = match_node;
		}

		for (AnnotationNode *annotation : match_branch_annotation_stack) {
			branch->annotations.push_back(annotation);
		}
		match_branch_annotation_stack.clear();

#ifdef DEBUG_ENABLED
		if (have_wildcard && !branch->patterns.is_empty()) {
			push_warning(branch->patterns[0], BSWarning::UNREACHABLE_PATTERN);
		}
#endif

		have_wildcard = have_wildcard || branch->has_wildcard;
		all_have_return = all_have_return && branch->block->has_return;
		match_node->branches.push_back(branch);
	}
	complete_extents(match_node);

	consume(BSTokenizer::Token::DEDENT, R"(Expected an indented block after "match" statement.)");

	if (all_have_return && have_wildcard) {
		current_suite->has_return = true;
	}

	for (const AnnotationNode *annotation : match_branch_annotation_stack) {
		push_error(vformat(R"(Annotation "%s" does not precede a valid target, so it will have no effect.)", annotation->name), annotation);
	}
	match_branch_annotation_stack.clear();

	return match_node;
}

BSParser::MatchBranchNode *BSParser::parse_match_branch() {
	MatchBranchNode *branch = alloc_node<MatchBranchNode>();
	reset_extents(branch, current);

	bool has_bind = false;

	// A contextual case shorthand completed in pattern position is filtered against the cases the
	// enclosing match already handles, so the branch it sits in has to be recorded. The context is
	// only this branch's when the pattern list is what produced it.
	const CompletionType completion_type_before_patterns = completion_context.type;
	const Node *completion_node_before_patterns = completion_context.node;

	do {
		PatternNode *pattern = parse_match_pattern();
		if (pattern == nullptr) {
			continue;
		}
		if (pattern->binds.size() > 0) {
			has_bind = true;
		}
		if (branch->patterns.size() > 0 && has_bind) {
			push_error(R"(Cannot use a variable bind with multiple patterns.)");
		}
		if (pattern->pattern_type == PatternNode::PT_REST) {
			push_error(R"(Rest pattern can only be used inside array and dictionary patterns.)");
		} else if (pattern->pattern_type == PatternNode::PT_BIND || pattern->pattern_type == PatternNode::PT_WILDCARD) {
			branch->has_wildcard = true;
		}
		branch->patterns.push_back(pattern);
	} while (match(BSTokenizer::Token::COMMA));

	if (completion_context.type == COMPLETION_CONTEXTUAL_UNION_CASE && completion_context.match_branch == nullptr &&
			(completion_context.type != completion_type_before_patterns || completion_context.node != completion_node_before_patterns)) {
		completion_context.match_branch = branch;
	}

	if (branch->patterns.is_empty()) {
		push_error(R"(No pattern found for "match" branch.)");
	}

	bool has_guard = false;
	if (match(BSTokenizer::Token::WHEN)) {
		// Pattern guard.
		// Create block for guard because it also needs to access the bound variables from patterns, and we don't want to add them to the outer scope.
		branch->guard_body = alloc_node<SuiteNode>();
		if (branch->patterns.size() > 0) {
			for (const KeyValue<StringName, IdentifierNode *> &E : branch->patterns[0]->binds) {
				SuiteNode::Local local(E.value, current_function);
				local.type = SuiteNode::Local::PATTERN_BIND;
				branch->guard_body->add_local(local);
			}
		}

		SuiteNode *parent_block = current_suite;
		branch->guard_body->parent_block = parent_block;
		current_suite = branch->guard_body;

		ExpressionNode *guard = parse_expression(false);
		if (guard == nullptr) {
			push_error(R"(Expected expression for pattern guard after "when".)");
		} else {
			branch->guard_body->statements.append(guard);
		}
		current_suite = parent_block;
		complete_extents(branch->guard_body);

		has_guard = true;
		branch->has_wildcard = false; // If it has a guard, the wildcard might still not match.
	}

	if (!consume(BSTokenizer::Token::COLON, vformat(R"(Expected ":"%s after "match" %s.)", has_guard ? "" : R"( or "when")", has_guard ? "pattern guard" : "patterns"))) {
		branch->block = alloc_recovery_suite();
		complete_extents(branch);
		// Consume the whole line and treat the next one as new match branch.
		while (current.type != BSTokenizer::Token::NEWLINE && !is_at_end()) {
			advance();
		}
		if (!is_at_end()) {
			advance();
		}
		return branch;
	}

	SuiteNode *suite = alloc_node<SuiteNode>();
	if (branch->patterns.size() > 0) {
		for (const KeyValue<StringName, IdentifierNode *> &E : branch->patterns[0]->binds) {
			SuiteNode::Local local(E.value, current_function);
			local.type = SuiteNode::Local::PATTERN_BIND;
			suite->add_local(local);
		}
	}

	branch->block = parse_suite("match pattern block", suite);
	complete_extents(branch);

	return branch;
}

BSParser::PatternNode *BSParser::parse_match_pattern(PatternNode *p_root_pattern) {
	// Array and dictionary patterns recurse through parse_match_pattern(); bound that
	// depth so deeply nested patterns report an error instead of overflowing the stack.
	RecursionDepthGuard depth_guard(pattern_nesting_depth);
	if (unlikely(pattern_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Pattern nesting is too deep.");
		return nullptr;
	}

	PatternNode *pattern = alloc_node<PatternNode>();
	reset_extents(pattern, current);

	switch (current.type) {
		case BSTokenizer::Token::VAR: {
			// Bind.
			advance();
			if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected bind name after "var".)")) {
				complete_extents(pattern);
				return nullptr;
			}
			pattern->pattern_type = PatternNode::PT_BIND;
			pattern->bind = parse_identifier();

			PatternNode *root_pattern = p_root_pattern == nullptr ? pattern : p_root_pattern;

			if (p_root_pattern != nullptr) {
				if (p_root_pattern->has_bind(pattern->bind->name)) {
					push_error(vformat(R"(Bind variable name "%s" was already used in this pattern.)", pattern->bind->name));
					complete_extents(pattern);
					return nullptr;
				}
			}

			if (current_suite->has_local(pattern->bind->name)) {
				push_error(vformat(R"(There's already a %s named "%s" in this scope.)", current_suite->get_local(pattern->bind->name).get_name(), pattern->bind->name));
				complete_extents(pattern);
				return nullptr;
			}

			root_pattern->binds[pattern->bind->name] = pattern->bind;

		} break;
		case BSTokenizer::Token::PARENTHESIS_OPEN: {
			// Tuple pattern, or an ordinary parenthesized grouping when it holds a single pattern.
			push_multiline(true);
			advance();
			pattern->pattern_type = PatternNode::PT_TUPLE;

			PatternNode *root_pattern = p_root_pattern != nullptr ? p_root_pattern : pattern;
			if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
				push_error(R"(A tuple pattern cannot be empty.)");
			} else {
				PatternNode *first_element = parse_match_pattern(root_pattern);
				if (first_element != nullptr) {
					pattern->array.push_back(first_element);
				}
				if (!check(BSTokenizer::Token::COMMA) && first_element != nullptr) {
					// Grouping: `(p)` is the pattern `p` itself, never a one-element tuple.
					pop_multiline();
					consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after the grouped pattern.)*");

					ExpressionNode *grouped_expression = nullptr;
					if (first_element->pattern_type == PatternNode::PT_LITERAL) {
						grouped_expression = first_element->literal;
					} else if (first_element->pattern_type == PatternNode::PT_EXPRESSION) {
						grouped_expression = first_element->expression;
					}
					if (grouped_expression != nullptr && get_rule(current.type)->precedence != PREC_NONE) {
						// The grouping was only the head of a larger value pattern, e.g. `(A + B) * C`.
						pattern->array.clear();
						ExpressionNode *expression = parse_infix_operators(grouped_expression, PREC_ASSIGNMENT, false);
						pattern->pattern_type = expression != nullptr && expression->type == Node::LITERAL ? PatternNode::PT_LITERAL : PatternNode::PT_EXPRESSION;
						pattern->expression = expression;
						complete_extents(pattern);
						return pattern;
					}

					// The grouping node is dropped, so any bind it collected as the root pattern moves to
					// the pattern that takes its place; the branch reads its binds off that node.
					if (p_root_pattern == nullptr) {
						for (const KeyValue<StringName, IdentifierNode *> &E : pattern->binds) {
							first_element->binds[E.key] = E.value;
						}
					}
					first_element->was_grouped = true;
					complete_extents(pattern);
					return first_element;
				}
				while (match(BSTokenizer::Token::COMMA)) {
					if (is_at_end() || check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
						break; // Trailing comma.
					}
					PatternNode *sub_pattern = parse_match_pattern(root_pattern);
					if (sub_pattern == nullptr) {
						continue;
					}
					if (sub_pattern->pattern_type == PatternNode::PT_REST) {
						push_error(R"(The ".." pattern cannot be used in a tuple pattern.)");
					} else {
						pattern->array.push_back(sub_pattern);
					}
				}
			}

			pop_multiline();
			consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after the tuple pattern.)*");
			if (pattern->array.size() == 1) {
				push_error(R"(A single-element tuple pattern is not allowed; either drop the trailing comma or add a second element.)", pattern);
			}
		} break;
		case BSTokenizer::Token::UNDERSCORE:
			// Wildcard.
			advance();
			pattern->pattern_type = PatternNode::PT_WILDCARD;
			break;
		case BSTokenizer::Token::PERIOD_PERIOD:
			// Rest.
			advance();
			pattern->pattern_type = PatternNode::PT_REST;
			break;
		case BSTokenizer::Token::BRACKET_OPEN: {
			// Array.
			push_multiline(true);
			advance();
			pattern->pattern_type = PatternNode::PT_ARRAY;
			do {
				if (is_at_end() || check(BSTokenizer::Token::BRACKET_CLOSE)) {
					break;
				}
				PatternNode *sub_pattern = parse_match_pattern(p_root_pattern != nullptr ? p_root_pattern : pattern);
				if (sub_pattern == nullptr) {
					continue;
				}
				if (pattern->rest_used) {
					push_error(R"(The ".." pattern must be the last element in the pattern array.)");
				} else if (sub_pattern->pattern_type == PatternNode::PT_REST) {
					pattern->rest_used = true;
				}
				pattern->array.push_back(sub_pattern);
			} while (match(BSTokenizer::Token::COMMA));
			consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected "]" to close the array pattern.)");
			pop_multiline();
			break;
		}
		case BSTokenizer::Token::BRACE_OPEN: {
			// Dictionary.
			push_multiline(true);
			advance();
			pattern->pattern_type = PatternNode::PT_DICTIONARY;
			do {
				if (check(BSTokenizer::Token::BRACE_CLOSE) || is_at_end()) {
					break;
				}
				if (match(BSTokenizer::Token::PERIOD_PERIOD)) {
					// Rest.
					if (pattern->rest_used) {
						push_error(R"(The ".." pattern must be the last element in the pattern dictionary.)");
					} else {
						PatternNode *sub_pattern = alloc_node<PatternNode>();
						complete_extents(sub_pattern);
						sub_pattern->pattern_type = PatternNode::PT_REST;
						pattern->dictionary.push_back({ nullptr, sub_pattern });
						pattern->rest_used = true;
					}
				} else {
					ExpressionNode *key = parse_expression(false);
					if (key == nullptr) {
						push_error(R"(Expected expression as key for dictionary pattern.)");
					}
					if (match(BSTokenizer::Token::COLON)) {
						// Value pattern.
						PatternNode *sub_pattern = parse_match_pattern(p_root_pattern != nullptr ? p_root_pattern : pattern);
						if (sub_pattern == nullptr) {
							continue;
						}
						if (pattern->rest_used) {
							push_error(R"(The ".." pattern must be the last element in the pattern dictionary.)");
						} else if (sub_pattern->pattern_type == PatternNode::PT_REST) {
							push_error(R"(The ".." pattern cannot be used as a value.)");
						} else {
							pattern->dictionary.push_back({ key, sub_pattern });
						}
					} else {
						// Key match only.
						pattern->dictionary.push_back({ key, nullptr });
					}
				}
			} while (match(BSTokenizer::Token::COMMA));
			consume(BSTokenizer::Token::BRACE_CLOSE, R"(Expected "}" to close the dictionary pattern.)");
			pop_multiline();
			break;
		}
		default: {
			// A dotted name directly followed by `(` is a tagged-union case pattern; anything else is
			// an ordinary value pattern, so the dotted head is handed back to the expression parser.
			// A bracketed head (`Result[int, String].Ok(...)`) applies a generic union's type arguments
			// before the dotted case name, and is disambiguated from an indexed value pattern there.
			if (current.is_identifier() && (peek().type == BSTokenizer::Token::PERIOD || peek().type == BSTokenizer::Token::BRACKET_OPEN)) {
				parse_match_pattern_dotted_head(pattern, p_root_pattern);
				break;
			}

			// A leading `.` names a case of the subject's union without naming the union itself.
			if (check(BSTokenizer::Token::PERIOD) && peek().is_identifier()) {
				parse_match_pattern_dotted_head(pattern, p_root_pattern);
				break;
			}

			// Expression.
			ExpressionNode *expression = parse_expression(false);
			if (expression == nullptr) {
				push_error(R"(Expected expression for match pattern.)");
				complete_extents(pattern);
				return nullptr;
			} else {
				if (expression->type == BSParser::Node::LITERAL) {
					pattern->pattern_type = PatternNode::PT_LITERAL;
				} else {
					pattern->pattern_type = PatternNode::PT_EXPRESSION;
				}
				pattern->expression = expression;
			}
			break;
		}
	}
	complete_extents(pattern);

	return pattern;
}

void BSParser::parse_match_pattern_dotted_head(PatternNode *p_pattern, PatternNode *p_root_pattern) {
	// The head is a dotted name, `A.B` or longer, or the leading-`.` contextual case shorthand
	// (`.Ok`), which names a single case of the subject's union. Either is a case reference only
	// when `(` follows it immediately; otherwise it is the start of an ordinary value pattern such
	// as `Vector2.ZERO`.
	const bool is_contextual_case = check(BSTokenizer::Token::PERIOD);

	ExpressionNode *head_expression = nullptr;
	Vector<IdentifierNode *> case_chain;
	if (is_contextual_case) {
		advance(); // Consume ".", so `parse_contextual_enum_case()` sees the same tokenizer state as the Pratt driver.
		head_expression = parse_contextual_enum_case(nullptr, false);
		const SubscriptNode *contextual_case = head_expression != nullptr && head_expression->type == Node::SUBSCRIPT
				? static_cast<const SubscriptNode *>(head_expression)
				: nullptr;
		if (contextual_case != nullptr && contextual_case->is_attribute && contextual_case->attribute != nullptr) {
			case_chain.push_back(contextual_case->attribute);
		}
	} else {
		advance();
		head_expression = parse_identifier(nullptr, false);
#ifdef TOOLS_ENABLED
		if (head_expression != nullptr) {
			make_completion_context(COMPLETION_IDENTIFIER, head_expression);
		}
#endif
		if (head_expression != nullptr && head_expression->type == Node::IDENTIFIER) {
			case_chain.push_back(static_cast<IdentifierNode *>(head_expression));
		}
	}

	// Type arguments applied to a generic tagged union before its case name. Only the trailing `(`
	// below tells `Result[int, String].Ok(value)` apart from an indexed value pattern such as
	// `TABLE[KEY]`, and one token of lookahead cannot see that far, so the brackets are parsed as an
	// ordinary subscript and their arguments are carried as expressions to be read as types later.
	Vector<ExpressionNode *> type_argument_expressions;
	Vector<bool> type_argument_expression_is_nullable;
	int type_argument_chain_index = 0;
	bool parsed_type_arguments = false;
	// The argument list binds to the last name of the head, so a qualified union
	// (`Outer.Result[int, String].Ok(v)`) is spelled the same way it is in a type annotation.
	auto parse_head_type_arguments = [&]() {
		push_multiline(true);
		advance(); // Consume "[", so `parse_subscript()` sees the same tokenizer state as the Pratt driver.
		head_expression = parse_subscript(head_expression, false);
		const SubscriptNode *application = head_expression != nullptr && head_expression->type == Node::SUBSCRIPT
				? static_cast<const SubscriptNode *>(head_expression)
				: nullptr;
		if (application == nullptr || application->is_attribute) {
			case_chain.clear();
		} else if (!application->type_arguments.is_empty()) {
			type_argument_expressions = application->type_arguments;
			type_argument_expression_is_nullable = application->type_argument_is_nullable;
		} else if (application->index != nullptr) {
			// A single-argument list keeps aliasing `index`, exactly as `Box[int]` does in value position.
			type_argument_expressions.push_back(application->index);
			type_argument_expression_is_nullable.push_back(false);
		} else {
			case_chain.clear();
		}
		type_argument_chain_index = case_chain.is_empty() ? 0 : case_chain.size() - 1;
		parsed_type_arguments = true;
	};

	// The contextual shorthand names exactly one case and never spells its union, so it takes
	// neither a type-argument list nor a longer dotted chain. Anything following it is left to the
	// expression path below, which keeps the leading `.` in the tree.
	if (!is_contextual_case && head_expression != nullptr && check(BSTokenizer::Token::BRACKET_OPEN)) {
		parse_head_type_arguments();
	}

	while (!is_contextual_case && head_expression != nullptr && check(BSTokenizer::Token::PERIOD) && peek().is_identifier()) {
		advance(); // Consume ".", so `parse_attribute()` sees the same tokenizer state as the Pratt driver.
		head_expression = parse_attribute(head_expression, false);
		if (head_expression == nullptr || head_expression->type != Node::SUBSCRIPT) {
			case_chain.clear();
			break;
		}
		SubscriptNode *attribute = static_cast<SubscriptNode *>(head_expression);
		if (!attribute->is_attribute || attribute->attribute == nullptr) {
			case_chain.clear();
			break;
		}
		if (!case_chain.is_empty()) {
			case_chain.push_back(attribute->attribute);
		}
		if (!parsed_type_arguments && check(BSTokenizer::Token::BRACKET_OPEN)) {
			parse_head_type_arguments();
		}
	}

	const int minimum_case_chain_size = is_contextual_case ? 1 : 2;
	if (head_expression != nullptr && case_chain.size() >= minimum_case_chain_size && check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
		// The case reference is resolved as a type, exactly like the right-hand side of `is`, so a
		// payload case is never reduced as a value.
		TypeNode *case_type = alloc_node<TypeNode>();
		reset_extents(case_type, case_chain[0]);
		update_extents(case_type);
		case_type->type_chain = case_chain;
		case_type->allows_enum_case = true;
		case_type->type_argument_expressions = type_argument_expressions;
		case_type->type_argument_expression_is_nullable = type_argument_expression_is_nullable;
		case_type->type_arguments_chain_index = type_argument_chain_index;
		complete_extents(case_type);

		p_pattern->pattern_type = PatternNode::PT_ENUM_CASE;
		p_pattern->case_type = case_type;
		p_pattern->is_contextual_enum_case = is_contextual_case;

		// A payload pattern rebuilds its contextual head as this case type, and it is the case type the
		// analyzer resolves. Point a context the head produced at the node that ends up carrying the union.
		if (is_contextual_case && completion_context.type == COMPLETION_CONTEXTUAL_UNION_CASE &&
				completion_context.node == head_expression) {
			completion_context.node = case_type;
		}

		PatternNode *root_pattern = p_root_pattern != nullptr ? p_root_pattern : p_pattern;

		push_multiline(true);
		advance(); // Consume "(".
		if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
			push_error(R"(Expected at least one payload pattern after "(".)");
		} else {
			do {
				if (is_at_end() || check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
					break; // Trailing comma.
				}
				// A bare identifier in a payload position binds the value, matching `is Case(x, y)`.
				// Any other expression keeps its usual meaning as a value pattern.
				PatternNode *sub_pattern = nullptr;
				if (current.is_identifier() && (peek().type == BSTokenizer::Token::COMMA || peek().type == BSTokenizer::Token::PARENTHESIS_CLOSE)) {
					sub_pattern = parse_match_case_payload_bind(root_pattern);
				} else {
					sub_pattern = parse_match_pattern(root_pattern);
				}
				if (sub_pattern == nullptr) {
					continue;
				}
				if (sub_pattern->pattern_type == PatternNode::PT_REST) {
					push_error(R"(The ".." pattern cannot be used in a case pattern.)");
				} else {
					p_pattern->array.push_back(sub_pattern);
				}
			} while (match(BSTokenizer::Token::COMMA));
		}
		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after the case pattern.)*");
		return;
	}

	ExpressionNode *expression = parse_infix_operators(head_expression, PREC_ASSIGNMENT, false);
	if (expression == nullptr) {
		push_error(R"(Expected expression for match pattern.)");
		return;
	}
	p_pattern->pattern_type = expression->type == Node::LITERAL ? PatternNode::PT_LITERAL : PatternNode::PT_EXPRESSION;
	p_pattern->expression = expression;
}

BSParser::PatternNode *BSParser::parse_match_case_payload_bind(PatternNode *p_root_pattern) {
	PatternNode *pattern = alloc_node<PatternNode>();
	reset_extents(pattern, current);
	advance();

	pattern->pattern_type = PatternNode::PT_BIND;
	pattern->implicit_bind = true;
	pattern->bind = parse_identifier();
	if (pattern->bind == nullptr) {
		complete_extents(pattern);
		return nullptr;
	}

	if (p_root_pattern->has_bind(pattern->bind->name)) {
		push_error(vformat(R"(Bind variable name "%s" was already used in this pattern.)", pattern->bind->name));
		complete_extents(pattern);
		return nullptr;
	}
	if (current_suite->has_local(pattern->bind->name)) {
		push_error(vformat(R"(There's already a %s named "%s" in this scope.)", current_suite->get_local(pattern->bind->name).get_name(), pattern->bind->name));
		complete_extents(pattern);
		return nullptr;
	}

	p_root_pattern->binds[pattern->bind->name] = pattern->bind;
	complete_extents(pattern);
	return pattern;
}

bool BSParser::PatternNode::has_bind(const StringName &p_name) {
	return binds.has(p_name);
}

BSParser::IdentifierNode *BSParser::PatternNode::get_bind(const StringName &p_name) {
	return binds[p_name];
}

BSParser::WhileNode *BSParser::parse_while() {
	WhileNode *n_while = alloc_node<WhileNode>();

	int case_binds_mark = pending_case_binds.size();
	{
		RecursionDepthGuard case_bind_condition_guard(case_bind_condition_depth);
		n_while->condition = parse_expression(false);
	}
	if (n_while->condition == nullptr) {
		push_error(R"(Expected conditional expression after "while".)");
	}

	Vector<TypeTestNode *> case_bind_tests;
	collect_condition_case_binds(n_while->condition, case_bind_tests);

	consume(BSTokenizer::Token::COLON, R"(Expected ":" after "while" condition.)");

	// Save break/continue state.
	bool could_break = can_break;
	bool could_continue = can_continue;

	// Allow break/continue.
	can_break = true;
	can_continue = true;

	SuiteNode *suite = alloc_node<SuiteNode>();
	suite->is_in_loop = true;
	n_while->condition_has_case_binds = declare_condition_case_binds(case_bind_tests, suite, case_binds_mark);
	n_while->loop = parse_suite(R"("while" block)", suite);
	complete_extents(n_while);

	// Reset break/continue state.
	can_break = could_break;
	can_continue = could_continue;

	return n_while;
}

BSParser::ExpressionNode *BSParser::parse_precedence(Precedence p_precedence, bool p_can_assign, bool p_stop_on_assign, bool p_stop_on_question_mark) {
	// Bail out before the recursive descent overflows the native stack on pathologically
	// nested expressions (e.g. thousands of nested parentheses), turning a crash into a
	// recoverable parse error.
	RecursionDepthGuard depth_guard(expression_nesting_depth);
	if (unlikely(expression_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Expression nesting is too deep.");
		return nullptr;
	}

	// Switch multiline mode on for grouping tokens.
	// Do this early to avoid the tokenizer generating whitespace tokens.
	switch (current.type) {
		case BSTokenizer::Token::PARENTHESIS_OPEN:
		case BSTokenizer::Token::BRACE_OPEN:
		case BSTokenizer::Token::BRACKET_OPEN:
			push_multiline(true);
			break;
		default:
			break; // Nothing to do.
	}

	// Completion can appear whenever an expression is expected.
	make_completion_context(COMPLETION_IDENTIFIER, nullptr, -1, false);

	BSTokenizer::Token token = current;
	BSTokenizer::Token::Type token_type = token.type;
	if (token.is_identifier()) {
		// Allow keywords that can be treated as identifiers.
		token_type = BSTokenizer::Token::IDENTIFIER;
	}
	ParseFunction prefix_rule = get_rule(token_type)->prefix;

	if (prefix_rule == nullptr) {
		// Expected expression. Let the caller give the proper error message.
		return nullptr;
	}

	advance(); // Only consume the token if there's a valid rule.

	// After a token was consumed, update the completion context regardless of a previously set context.

	ExpressionNode *previous_operand = (this->*prefix_rule)(nullptr, p_can_assign);

#ifdef TOOLS_ENABLED
	// HACK: We can't create a context in parse_identifier since it is used in places were we don't want completion.
	if (previous_operand != nullptr && previous_operand->type == BSParser::Node::IDENTIFIER && prefix_rule == static_cast<ParseFunction>(&BSParser::parse_identifier)) {
		make_completion_context(COMPLETION_IDENTIFIER, previous_operand);
	}
#endif

	return parse_infix_operators(previous_operand, p_precedence, p_can_assign, p_stop_on_assign, p_stop_on_question_mark);
}

BSParser::ExpressionNode *BSParser::parse_infix_operators(ExpressionNode *p_previous_operand, Precedence p_precedence, bool p_can_assign, bool p_stop_on_assign, bool p_stop_on_question_mark) {
	ExpressionNode *previous_operand = p_previous_operand;

	while (p_precedence <= get_rule(current.type)->precedence) {
		if (previous_operand == nullptr || (p_stop_on_assign && current.type == BSTokenizer::Token::EQUAL) || (p_stop_on_question_mark && current.type == BSTokenizer::Token::QUESTION_MARK) || lambda_ended) {
			return previous_operand;
		}
		// Also switch multiline mode on here for infix operators.
		switch (current.type) {
			// case BSTokenizer::Token::BRACE_OPEN: // Not an infix operator.
			case BSTokenizer::Token::PARENTHESIS_OPEN:
			case BSTokenizer::Token::BRACKET_OPEN:
				push_multiline(true);
				break;
			default:
				break; // Nothing to do.
		}
		const BSTokenizer::Token token = advance();
		ParseFunction infix_rule = get_rule(token.type)->infix;
		previous_operand = (this->*infix_rule)(previous_operand, p_can_assign);
	}

	return previous_operand;
}

BSParser::ExpressionNode *BSParser::parse_expression(bool p_can_assign, bool p_stop_on_assign, bool p_stop_on_question_mark) {
	return parse_precedence(PREC_ASSIGNMENT, p_can_assign, p_stop_on_assign, p_stop_on_question_mark);
}

BSParser::IdentifierNode *BSParser::parse_identifier() {
	IdentifierNode *identifier = static_cast<IdentifierNode *>(parse_identifier(nullptr, false));
#ifdef DEBUG_ENABLED
	// Check for spoofing here (if available in TextServer) since this isn't called inside expressions. This is only relevant for declarations.
	// Upstream reaches the primary interface through the engine-internal `TS` macro
	// (fs_parser.cpp:4877 @ c9d5e35); the registry wraps the same three calls behind a public route.
	if (identifier && BSWarning::is_confusable_identifier(identifier->name)) {
		push_warning(identifier, BSWarning::CONFUSABLE_IDENTIFIER, String(identifier->name));
	}
#endif
	return identifier;
}

BSParser::ExpressionNode *BSParser::parse_identifier(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (!previous.is_identifier()) {
		ERR_FAIL_V_MSG(nullptr, "Parser bug: parsing identifier node without identifier token.");
	}
	IdentifierNode *identifier = alloc_node<IdentifierNode>();
	complete_extents(identifier);
	identifier->name = previous.get_identifier();
	identifier->suite = current_suite;

	if (current_suite != nullptr && current_suite->has_local(identifier->name)) {
		const SuiteNode::Local &declaration = current_suite->get_local(identifier->name);

		identifier->source_function = declaration.source_function;
		switch (declaration.type) {
			case SuiteNode::Local::CONSTANT:
				identifier->source = IdentifierNode::LOCAL_CONSTANT;
				identifier->constant_source = declaration.constant;
				declaration.constant->usages++;
				break;
			case SuiteNode::Local::VARIABLE:
				identifier->source = IdentifierNode::LOCAL_VARIABLE;
				identifier->variable_source = declaration.variable;
				declaration.variable->usages++;
				break;
			case SuiteNode::Local::PARAMETER:
				identifier->source = IdentifierNode::FUNCTION_PARAMETER;
				identifier->parameter_source = declaration.parameter;
				declaration.parameter->usages++;
				break;
			case SuiteNode::Local::FOR_VARIABLE:
				identifier->source = IdentifierNode::LOCAL_ITERATOR;
				identifier->bind_source = declaration.bind;
				declaration.bind->usages++;
				break;
			case SuiteNode::Local::PATTERN_BIND:
			case SuiteNode::Local::CASE_BIND:
				identifier->source = IdentifierNode::LOCAL_BIND;
				identifier->bind_source = declaration.bind;
				declaration.bind->usages++;
				break;
			case SuiteNode::Local::UNDEFINED:
				ERR_FAIL_V_MSG(nullptr, "Undefined local found.");
		}
	}

	return identifier;
}

BSParser::LiteralNode *BSParser::parse_literal() {
	return static_cast<LiteralNode *>(parse_literal(nullptr, false));
}

BSParser::ExpressionNode *BSParser::parse_literal(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (previous.type != BSTokenizer::Token::LITERAL) {
		push_error("Parser bug: parsing literal node without literal token.");
		ERR_FAIL_V_MSG(nullptr, "Parser bug: parsing literal node without literal token.");
	}

	LiteralNode *literal = alloc_node<LiteralNode>();
	// D1: upstream copies the token's width descriptor onto the node here (fs_parser.cpp:4966-4967
	// @ c9d5e35). With one integer type the `Variant` carrier is the whole of the literal's type.
	literal->value = previous.literal;
	reset_extents(literal, p_previous_operand);
	update_extents(literal);
	make_completion_context(COMPLETION_NONE, literal, -1);
	complete_extents(literal);
	return literal;
}

BSParser::ExpressionNode *BSParser::parse_self(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (current_function && current_function->is_static) {
		push_error(R"(Cannot use "self" inside a static function.)");
	}
	SelfNode *self = alloc_node<SelfNode>();
	complete_extents(self);
	self->current_class = current_class;
	return self;
}

BSParser::ExpressionNode *BSParser::parse_builtin_constant(ExpressionNode *p_previous_operand, bool p_can_assign) {
	BSTokenizer::Token::Type op_type = previous.type;
	LiteralNode *constant = alloc_node<LiteralNode>();
	complete_extents(constant);

	switch (op_type) {
		case BSTokenizer::Token::CONST_PI:
			constant->value = Math::PI;
			break;
		case BSTokenizer::Token::CONST_TAU:
			constant->value = Math::TAU;
			break;
		case BSTokenizer::Token::CONST_INF:
			constant->value = Math::INF;
			break;
		case BSTokenizer::Token::CONST_NAN:
			constant->value = Math::NaN;
			break;
		default:
			return nullptr; // Unreachable.
	}

	return constant;
}

BSParser::ExpressionNode *BSParser::parse_unary_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	BSTokenizer::Token::Type op_type = previous.type;
	UnaryOpNode *operation = alloc_node<UnaryOpNode>();

	switch (op_type) {
		case BSTokenizer::Token::MINUS:
			operation->operation = UnaryOpNode::OP_NEGATIVE;
			operation->variant_op = Variant::OP_NEGATE;
			operation->operand = parse_precedence(PREC_SIGN, false);
			if (operation->operand == nullptr) {
				push_error(R"(Expected expression after "-" operator.)");
			}
			break;
		case BSTokenizer::Token::PLUS:
			operation->operation = UnaryOpNode::OP_POSITIVE;
			operation->variant_op = Variant::OP_POSITIVE;
			operation->operand = parse_precedence(PREC_SIGN, false);
			if (operation->operand == nullptr) {
				push_error(R"(Expected expression after "+" operator.)");
			}
			break;
		case BSTokenizer::Token::TILDE:
			operation->operation = UnaryOpNode::OP_COMPLEMENT;
			operation->variant_op = Variant::OP_BIT_NEGATE;
			operation->operand = parse_precedence(PREC_BIT_NOT, false);
			if (operation->operand == nullptr) {
				push_error(R"(Expected expression after "~" operator.)");
			}
			break;
		case BSTokenizer::Token::NOT:
		case BSTokenizer::Token::BANG:
			operation->operation = UnaryOpNode::OP_LOGIC_NOT;
			operation->variant_op = Variant::OP_NOT;
			operation->operand = parse_precedence(PREC_LOGIC_NOT, false);
			if (operation->operand == nullptr) {
				push_error(vformat(R"(Expected expression after "%s" operator.)", op_type == BSTokenizer::Token::NOT ? "not" : "!"));
			}
			break;
		default:
			complete_extents(operation);
			return nullptr; // Unreachable.
	}
	complete_extents(operation);

	return operation;
}

BSParser::ExpressionNode *BSParser::parse_binary_not_in_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// check that NOT is followed by IN by consuming it before calling parse_binary_operator which will only receive a plain IN
	UnaryOpNode *operation = alloc_node<UnaryOpNode>();
	reset_extents(operation, p_previous_operand);
	update_extents(operation);
	consume(BSTokenizer::Token::TK_IN, R"(Expected "in" after "not" in content-test operator.)");
	ExpressionNode *in_operation = parse_binary_operator(p_previous_operand, p_can_assign);
	operation->operation = UnaryOpNode::OP_LOGIC_NOT;
	operation->variant_op = Variant::OP_NOT;
	operation->operand = in_operation;
	complete_extents(operation);
	return operation;
}

BSParser::ExpressionNode *BSParser::parse_binary_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	BSTokenizer::Token op = previous;
	BinaryOpNode *operation = alloc_node<BinaryOpNode>();
	reset_extents(operation, p_previous_operand);
	update_extents(operation);

	Precedence precedence = (Precedence)(get_rule(op.type)->precedence + 1);
	operation->left_operand = p_previous_operand;
	operation->right_operand = parse_precedence(precedence, false);
	complete_extents(operation);

	if (operation->right_operand == nullptr) {
		push_error(vformat(R"(Expected expression after "%s" operator.)", op.get_name()));
	}

	// TODO: Also for unary, ternary, and assignment.
	switch (op.type) {
		case BSTokenizer::Token::PLUS:
			operation->operation = BinaryOpNode::OP_ADDITION;
			operation->variant_op = Variant::OP_ADD;
			break;
		case BSTokenizer::Token::MINUS:
			operation->operation = BinaryOpNode::OP_SUBTRACTION;
			operation->variant_op = Variant::OP_SUBTRACT;
			break;
		case BSTokenizer::Token::STAR:
			operation->operation = BinaryOpNode::OP_MULTIPLICATION;
			operation->variant_op = Variant::OP_MULTIPLY;
			break;
		case BSTokenizer::Token::SLASH:
			operation->operation = BinaryOpNode::OP_DIVISION;
			operation->variant_op = Variant::OP_DIVIDE;
			break;
		case BSTokenizer::Token::PERCENT:
			operation->operation = BinaryOpNode::OP_MODULO;
			operation->variant_op = Variant::OP_MODULE;
			break;
		case BSTokenizer::Token::STAR_STAR:
			operation->operation = BinaryOpNode::OP_POWER;
			operation->variant_op = Variant::OP_POWER;
			break;
		case BSTokenizer::Token::LESS_LESS:
			operation->operation = BinaryOpNode::OP_BIT_LEFT_SHIFT;
			operation->variant_op = Variant::OP_SHIFT_LEFT;
			break;
		case BSTokenizer::Token::GREATER_GREATER:
			operation->operation = BinaryOpNode::OP_BIT_RIGHT_SHIFT;
			operation->variant_op = Variant::OP_SHIFT_RIGHT;
			break;
		case BSTokenizer::Token::AMPERSAND:
			operation->operation = BinaryOpNode::OP_BIT_AND;
			operation->variant_op = Variant::OP_BIT_AND;
			break;
		case BSTokenizer::Token::PIPE:
			operation->operation = BinaryOpNode::OP_BIT_OR;
			operation->variant_op = Variant::OP_BIT_OR;
			break;
		case BSTokenizer::Token::CARET:
			operation->operation = BinaryOpNode::OP_BIT_XOR;
			operation->variant_op = Variant::OP_BIT_XOR;
			break;
		case BSTokenizer::Token::AND:
		case BSTokenizer::Token::AMPERSAND_AMPERSAND:
			operation->operation = BinaryOpNode::OP_LOGIC_AND;
			operation->variant_op = Variant::OP_AND;
			break;
		case BSTokenizer::Token::OR:
		case BSTokenizer::Token::PIPE_PIPE:
			operation->operation = BinaryOpNode::OP_LOGIC_OR;
			operation->variant_op = Variant::OP_OR;
			break;
		case BSTokenizer::Token::TK_IN:
			operation->operation = BinaryOpNode::OP_CONTENT_TEST;
			operation->variant_op = Variant::OP_IN;
			break;
		case BSTokenizer::Token::EQUAL_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_EQUAL;
			operation->variant_op = Variant::OP_EQUAL;
			break;
		case BSTokenizer::Token::BANG_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_NOT_EQUAL;
			operation->variant_op = Variant::OP_NOT_EQUAL;
			break;
		case BSTokenizer::Token::LESS:
			operation->operation = BinaryOpNode::OP_COMP_LESS;
			operation->variant_op = Variant::OP_LESS;
			break;
		case BSTokenizer::Token::LESS_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_LESS_EQUAL;
			operation->variant_op = Variant::OP_LESS_EQUAL;
			break;
		case BSTokenizer::Token::GREATER:
			operation->operation = BinaryOpNode::OP_COMP_GREATER;
			operation->variant_op = Variant::OP_GREATER;
			break;
		case BSTokenizer::Token::GREATER_EQUAL:
			operation->operation = BinaryOpNode::OP_COMP_GREATER_EQUAL;
			operation->variant_op = Variant::OP_GREATER_EQUAL;
			break;
		default:
			return nullptr; // Unreachable.
	}

	return operation;
}

BSParser::ExpressionNode *BSParser::parse_ternary_operator(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// Only one ternary operation exists, so no abstraction here.
	TernaryOpNode *operation = alloc_node<TernaryOpNode>();
	reset_extents(operation, p_previous_operand);
	update_extents(operation);

	operation->true_expr = p_previous_operand;
	operation->condition = parse_precedence(PREC_TERNARY, false);

	if (operation->condition == nullptr) {
		push_error(R"(Expected expression as ternary condition after "if".)");
	}

	consume(BSTokenizer::Token::ELSE, R"(Expected "else" after ternary operator condition.)");

	operation->false_expr = parse_precedence(PREC_TERNARY, false);

	if (operation->false_expr == nullptr) {
		push_error(R"(Expected expression after "else".)");
	}

	complete_extents(operation);
	return operation;
}

BSParser::ExpressionNode *BSParser::parse_assignment(ExpressionNode *p_previous_operand, bool p_can_assign) {
	if (!p_can_assign) {
		push_error("Assignment is not allowed inside an expression.");
		return parse_expression(false); // Return the following expression.
	}
	if (p_previous_operand == nullptr) {
		return parse_expression(false); // Return the following expression.
	}

	switch (p_previous_operand->type) {
		case Node::IDENTIFIER: {
#ifdef DEBUG_ENABLED
			// Get source to store assignment count.
			// Also remove one usage since assignment isn't usage.
			IdentifierNode *id = static_cast<IdentifierNode *>(p_previous_operand);
			switch (id->source) {
				case IdentifierNode::LOCAL_VARIABLE:
					id->variable_source->usages--;
					break;
				case IdentifierNode::LOCAL_CONSTANT:
					id->constant_source->usages--;
					break;
				case IdentifierNode::FUNCTION_PARAMETER:
					id->parameter_source->usages--;
					break;
				case IdentifierNode::LOCAL_ITERATOR:
				case IdentifierNode::LOCAL_BIND:
					id->bind_source->usages--;
					break;
				default:
					break;
			}
#endif
		} break;
		case Node::SUBSCRIPT:
			// Okay.
			break;
		default:
			push_error(R"(Only identifier, attribute access, and subscription access can be used as assignment target.)");
			return parse_expression(false); // Return the following expression.
	}

	AssignmentNode *assignment = alloc_node<AssignmentNode>();
	reset_extents(assignment, p_previous_operand);
	update_extents(assignment);

	make_completion_context(COMPLETION_ASSIGN, assignment);
	switch (previous.type) {
		case BSTokenizer::Token::EQUAL:
			assignment->operation = AssignmentNode::OP_NONE;
			assignment->variant_op = Variant::OP_MAX;
			break;
		case BSTokenizer::Token::PLUS_EQUAL:
			assignment->operation = AssignmentNode::OP_ADDITION;
			assignment->variant_op = Variant::OP_ADD;
			break;
		case BSTokenizer::Token::MINUS_EQUAL:
			assignment->operation = AssignmentNode::OP_SUBTRACTION;
			assignment->variant_op = Variant::OP_SUBTRACT;
			break;
		case BSTokenizer::Token::STAR_EQUAL:
			assignment->operation = AssignmentNode::OP_MULTIPLICATION;
			assignment->variant_op = Variant::OP_MULTIPLY;
			break;
		case BSTokenizer::Token::STAR_STAR_EQUAL:
			assignment->operation = AssignmentNode::OP_POWER;
			assignment->variant_op = Variant::OP_POWER;
			break;
		case BSTokenizer::Token::SLASH_EQUAL:
			assignment->operation = AssignmentNode::OP_DIVISION;
			assignment->variant_op = Variant::OP_DIVIDE;
			break;
		case BSTokenizer::Token::PERCENT_EQUAL:
			assignment->operation = AssignmentNode::OP_MODULO;
			assignment->variant_op = Variant::OP_MODULE;
			break;
		case BSTokenizer::Token::LESS_LESS_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_SHIFT_LEFT;
			assignment->variant_op = Variant::OP_SHIFT_LEFT;
			break;
		case BSTokenizer::Token::GREATER_GREATER_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_SHIFT_RIGHT;
			assignment->variant_op = Variant::OP_SHIFT_RIGHT;
			break;
		case BSTokenizer::Token::AMPERSAND_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_AND;
			assignment->variant_op = Variant::OP_BIT_AND;
			break;
		case BSTokenizer::Token::PIPE_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_OR;
			assignment->variant_op = Variant::OP_BIT_OR;
			break;
		case BSTokenizer::Token::CARET_EQUAL:
			assignment->operation = AssignmentNode::OP_BIT_XOR;
			assignment->variant_op = Variant::OP_BIT_XOR;
			break;
		default:
			break; // Unreachable.
	}
	assignment->assignee = p_previous_operand;
	assignment->assigned_value = parse_expression(false);
#ifdef TOOLS_ENABLED
	if (assignment->assigned_value != nullptr && assignment->assigned_value->type == BSParser::Node::IDENTIFIER) {
		override_completion_context(assignment->assigned_value, COMPLETION_ASSIGN, assignment);
	}
#endif
	if (assignment->assigned_value == nullptr) {
		push_error(R"(Expected an expression after "=".)");
	}
	complete_extents(assignment);

	return assignment;
}

BSParser::ExpressionNode *BSParser::parse_await(ExpressionNode *p_previous_operand, bool p_can_assign) {
	AwaitNode *await = alloc_node<AwaitNode>();
	ExpressionNode *element = parse_precedence(PREC_AWAIT, false);
	if (element == nullptr) {
		push_error(R"(Expected signal or coroutine after "await".)");
	}
	await->to_await = element;
	complete_extents(await);

	if (current_function) { // Might be null in a getter or setter.
		current_function->is_coroutine = true;
	}

	return await;
}

BSParser::ExpressionNode *BSParser::parse_array(ExpressionNode *p_previous_operand, bool p_can_assign) {
	ArrayNode *array = alloc_node<ArrayNode>();

	if (!check(BSTokenizer::Token::BRACKET_CLOSE)) {
		do {
			if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			ExpressionNode *element = parse_expression(false);
			if (element == nullptr) {
				push_error(R"(Expected expression as array element.)");
			} else {
				array->elements.push_back(element);
			}
		} while (match(BSTokenizer::Token::COMMA) && !is_at_end());
	}
	pop_multiline();
	consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after array elements.)");
	complete_extents(array);

	return array;
}

BSParser::ExpressionNode *BSParser::parse_dictionary(ExpressionNode *p_previous_operand, bool p_can_assign) {
	DictionaryNode *dictionary = alloc_node<DictionaryNode>();

	bool decided_style = false;
	if (!check(BSTokenizer::Token::BRACE_CLOSE)) {
		do {
			if (check(BSTokenizer::Token::BRACE_CLOSE)) {
				// Allow for trailing comma.
				break;
			}

			// Key.
			ExpressionNode *key = parse_expression(false, true); // Stop on "=" so we can check for Lua table style.

			if (key == nullptr) {
				push_error(R"(Expected expression as dictionary key.)");
			}

			if (!decided_style) {
				switch (current.type) {
					case BSTokenizer::Token::COLON:
						dictionary->style = DictionaryNode::PYTHON_DICT;
						break;
					case BSTokenizer::Token::EQUAL:
						dictionary->style = DictionaryNode::LUA_TABLE;
						break;
					default:
						push_error(R"(Expected ":" or "=" after dictionary key.)");
						break;
				}
				decided_style = true;
			}

			switch (dictionary->style) {
				case DictionaryNode::LUA_TABLE:
					if (key != nullptr && key->type != Node::IDENTIFIER && key->type != Node::LITERAL) {
						push_error(R"(Expected identifier or string as Lua-style dictionary key (e.g "{ key = value }").)");
					}
					if (key != nullptr && key->type == Node::LITERAL && static_cast<LiteralNode *>(key)->value.get_type() != Variant::STRING) {
						push_error(R"(Expected identifier or string as Lua-style dictionary key (e.g "{ key = value }").)");
					}
					if (!match(BSTokenizer::Token::EQUAL)) {
						if (match(BSTokenizer::Token::COLON)) {
							push_error(R"(Expected "=" after dictionary key. Mixing dictionary styles is not allowed.)");
							advance(); // Consume wrong separator anyway.
						} else {
							push_error(R"(Expected "=" after dictionary key.)");
						}
					}
					if (key != nullptr) {
						key->is_constant = true;
						if (key->type == Node::IDENTIFIER) {
							key->reduced_value = static_cast<IdentifierNode *>(key)->name;
						} else if (key->type == Node::LITERAL) {
							key->reduced_value = StringName(static_cast<LiteralNode *>(key)->value);
						}
					}
					break;
				case DictionaryNode::PYTHON_DICT:
					if (!match(BSTokenizer::Token::COLON)) {
						if (match(BSTokenizer::Token::EQUAL)) {
							push_error(R"(Expected ":" after dictionary key. Mixing dictionary styles is not allowed.)");
							advance(); // Consume wrong separator anyway.
						} else {
							push_error(R"(Expected ":" after dictionary key.)");
						}
					}
					break;
			}

			// Value.
			ExpressionNode *value = parse_expression(false);
			if (value == nullptr) {
				push_error(R"(Expected expression as dictionary value.)");
			}

			if (key != nullptr && value != nullptr) {
				dictionary->elements.push_back({ key, value });
			}

			// Do phrase level recovery by inserting an imaginary expression for missing keys or values.
			// This ensures the successfully parsed expression is part of the AST and can be analyzed.
			if (key != nullptr && value == nullptr) {
				LiteralNode *dummy = alloc_recovery_node<LiteralNode>();
				dummy->value = Variant();

				dictionary->elements.push_back({ key, dummy });
			} else if (key == nullptr && value != nullptr) {
				LiteralNode *dummy = alloc_recovery_node<LiteralNode>();
				dummy->value = Variant();

				dictionary->elements.push_back({ dummy, value });
			}

		} while (match(BSTokenizer::Token::COMMA) && !is_at_end());
	}
	pop_multiline();
	consume(BSTokenizer::Token::BRACE_CLOSE, R"(Expected closing "}" after dictionary elements.)");
	complete_extents(dictionary);

	return dictionary;
}

BSParser::ExpressionNode *BSParser::parse_grouping(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// `previous` is the `(` token itself: the Pratt driver already consumed it before invoking
	// this prefix rule. Anchor a tuple literal's extents here since `alloc_node()` (used once we
	// know it is a tuple, not a grouping) would otherwise anchor to whatever token `previous`
	// has become by then.
	const BSTokenizer::Token open_paren = previous;

	if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
		// `()` is neither a valid grouping nor a valid tuple literal.
		push_error(R"(A tuple literal cannot be empty.)");
		advance();
		pop_multiline();
		TupleLiteralNode *empty_tuple = alloc_node<TupleLiteralNode>();
		reset_extents(empty_tuple, open_paren);
		update_extents(empty_tuple);
		complete_extents(empty_tuple);
		return empty_tuple;
	}

	ExpressionNode *first_element = parse_expression(false);
	if (first_element == nullptr) {
		push_error(R"(Expected grouping expression.)");
		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after grouping expression.)*");
		return nullptr;
	}

	if (!check(BSTokenizer::Token::COMMA)) {
		// Ordinary expression grouping: `(a)` evaluates to `a` itself. The grouping
		// carries no semantic effect, so it gets no AST node of its own; but a
		// comment trailing the opening delimiter (`(  # note`) would otherwise have
		// nowhere left to attach once the delimiter is gone, so record this level's
		// span for the formatter to check. Nested redundant groupings (`((a))`)
		// each record their own span here, outermost last, since this same call
		// chain visits the innermost one first and every enclosing `parse_grouping`
		// call adds its own span on top as it unwinds.
		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after grouping expression.)*");
		ExpressionNode::GroupingSpan span;
		span.open_line = open_paren.start_line;
		span.close_line = previous.start_line;
		span.close_column = previous.end_column;
		// `current` is the token immediately after the consumed `)`. Structural
		// line-boundary tokens are not source continuations; every other token on
		// the close's physical line proves that a later token, not this delimiter,
		// owns any trailing comment on that line.
		bool next_is_source_token = true;
		switch (current.type) {
			case BSTokenizer::Token::TK_EOF:
			case BSTokenizer::Token::ERROR:
			case BSTokenizer::Token::NEWLINE:
			case BSTokenizer::Token::INDENT:
			case BSTokenizer::Token::DEDENT:
				next_is_source_token = false;
				break;
			default:
				break;
		}
		const bool has_later_source_on_line = next_is_source_token && current.start_line == span.close_line &&
				current.start_column >= span.close_column;
		span.close_is_last_token_on_line = !has_later_source_on_line;
		first_element->redundant_groupings.push_back(span);
		return first_element;
	}

	// Tuple literal: `(a, b, ...)`, arity >= 2. Trailing commas are allowed once arity >= 2;
	// a single trailing comma after one element (`(a,)`) is a hard error, never a 1-tuple.
	TupleLiteralNode *tuple_literal = alloc_node<TupleLiteralNode>();
	reset_extents(tuple_literal, open_paren);
	tuple_literal->elements.push_back(first_element);

	while (match(BSTokenizer::Token::COMMA)) {
		if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
			break; // Trailing comma.
		}
		ExpressionNode *element = parse_expression(false);
		if (element == nullptr) {
			push_error(R"(Expected expression after "," in tuple literal.)");
			break;
		}
		tuple_literal->elements.push_back(element);
	}

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after tuple literal.)*");

	if (tuple_literal->elements.size() == 1) {
		push_error(R"(A single-element tuple literal is not allowed; either drop the trailing comma or add a second element.)", tuple_literal);
	}

	update_extents(tuple_literal);
	complete_extents(tuple_literal);
	return tuple_literal;
}

BSParser::ExpressionNode *BSParser::parse_attribute(ExpressionNode *p_previous_operand, bool p_can_assign) {
	SubscriptNode *attribute = alloc_node<SubscriptNode>();
	reset_extents(attribute, p_previous_operand);
	update_extents(attribute);

	if (for_completion) {
		bool is_builtin = false;
		if (p_previous_operand && p_previous_operand->type == Node::IDENTIFIER) {
			const IdentifierNode *id = static_cast<const IdentifierNode *>(p_previous_operand);
			Variant::Type builtin_type = get_builtin_type(id->name);
			if (builtin_type < Variant::VARIANT_MAX) {
				make_completion_context(COMPLETION_BUILT_IN_TYPE_CONSTANT_OR_STATIC_METHOD, builtin_type);
				is_builtin = true;
			}
		}
		if (!is_builtin) {
			// `base.➡(` is already a call site: only callables belong there, so record the
			// functions-only attribute context. A bare `base.➡` still wants every member.
			if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
				make_completion_context(COMPLETION_ATTRIBUTE_METHOD, attribute, -1);
			} else {
				make_completion_context(COMPLETION_ATTRIBUTE, attribute, -1);
			}
		}
	}

	attribute->base = p_previous_operand;

	// Tuple index access (`t.0`): the tokenizer already lexes a digit directly following a
	// value-preceded `.` as a bare decimal-integer `LITERAL`, never a float, so this is
	// unambiguous here.
	if (check(BSTokenizer::Token::LITERAL) && current.literal.get_type() == Variant::INT) {
		advance();
		LiteralNode *index_literal = alloc_node<LiteralNode>();
		index_literal->value = previous.literal;
		update_extents(index_literal);
		complete_extents(index_literal);

		attribute->is_tuple_index = true;
		attribute->index = index_literal;

		complete_extents(attribute);
		return attribute;
	}

	if (current.is_node_name()) {
		current.type = BSTokenizer::Token::IDENTIFIER;
	}
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected identifier after "." for attribute access.)")) {
		// Completing `base.➡(` still produced an attribute access; keep that shape so lookup and
		// later call parsing see the same node the completion context points at.
		if (for_completion && completion_context.type == COMPLETION_ATTRIBUTE_METHOD) {
			attribute->is_attribute = true;
		}
		complete_extents(attribute);
		return attribute;
	}

	attribute->is_attribute = true;
	attribute->attribute = parse_identifier();

	complete_extents(attribute);
	return attribute;
}

// Prefix rule for a leading "." (`.None`, and the callee of `.Ok(1)`): a tagged-union case named
// without its union. The union is supplied by the expected type at the consumer site, so the node
// deliberately has no base for the analyzer to qualify later.
//
// The tokenizer never routes a leading-dot number here: `.5` in expression position lexes as a
// single float `LITERAL`, and a `.` after a value lexes as an infix `PERIOD` (tuple index `t.0`).
BSParser::ExpressionNode *BSParser::parse_contextual_enum_case(ExpressionNode *p_previous_operand, bool p_can_assign) {
	SubscriptNode *contextual_case = alloc_node<SubscriptNode>();
	reset_extents(contextual_case, previous);
	update_extents(contextual_case);

	contextual_case->is_contextual_enum_case = true;

	// The shorthand names a case of a union it never spells, so the receiver a plain attribute would
	// offer is absent. Tooling gets its own context and takes the union from the analyzed shorthand.
	make_completion_context(COMPLETION_CONTEXTUAL_UNION_CASE, contextual_case, -1);

	if (current.is_node_name()) {
		current.type = BSTokenizer::Token::IDENTIFIER;
	}
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected a tagged-union case name after ".".)")) {
		complete_extents(contextual_case);
		return contextual_case;
	}

	contextual_case->is_attribute = true;
	contextual_case->attribute = parse_identifier();

	complete_extents(contextual_case);
	return contextual_case;
}

// The same shorthand on the right of an `is` test (`x is .Ok(value)`). A test type is a `TypeNode`
// rather than an expression, so it gets its own construction; the node carries only the case name,
// because the union comes from the tested operand's type.
BSParser::TypeNode *BSParser::parse_contextual_enum_case_type() {
	advance(); // Consume ".".

	TypeNode *type = alloc_node<TypeNode>();
	reset_extents(type, previous);
	update_extents(type);

	type->allows_enum_case = true;
	type->is_contextual_enum_case = true;

	make_completion_context(COMPLETION_CONTEXTUAL_UNION_CASE, type, -1);

	if (current.is_node_name()) {
		current.type = BSTokenizer::Token::IDENTIFIER;
	}
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected a tagged-union case name after ".".)")) {
		complete_extents(type);
		return type;
	}

	type->type_chain.push_back(parse_identifier());

	complete_extents(type);
	return type;
}

BSParser::ExpressionNode *BSParser::parse_subscript(ExpressionNode *p_previous_operand, bool p_can_assign) {
	SubscriptNode *subscript = alloc_node<SubscriptNode>();
	reset_extents(subscript, p_previous_operand);
	update_extents(subscript);

	make_completion_context(COMPLETION_SUBSCRIPT, subscript);

	subscript->base = p_previous_operand;
	// Stop before a trailing `?` so a use-site nullable type-argument marker (`Box[Node?]`) is not
	// swallowed by the Pratt loop as the always-invalid `?` infix; `parse_cast`/ternary indexing
	// (`arr[x as int]`, `arr[a if b else c]`) bind more tightly than `?` and still parse here.
	subscript->index = parse_expression(false, false, true);

#ifdef TOOLS_ENABLED
	if (subscript->index != nullptr && subscript->index->type == Node::LITERAL) {
		override_completion_context(subscript->index, COMPLETION_SUBSCRIPT, subscript);
	}
#endif

	if (subscript->index == nullptr) {
		push_error(R"(Expected expression after "[".)");
	} else if (check(BSTokenizer::Token::QUESTION_MARK) || check(BSTokenizer::Token::COMMA)) {
		// A use-site type-argument list such as `Pair[int, String]`, `id[Node?]`, or a Callable
		// signature argument. Ordinary indexing never uses commas or a trailing `?`, so this shape
		// is unambiguous here. Capture the full comma-separated list (with optional `?` markers) so
		// generic-class specialization and explicit generic-method application can read every
		// argument; `index` keeps aliasing the first element. Whether such a list is valid for the
		// subscripted base is decided during analysis, mirroring how `Box[int]` is interpreted.
		subscript->type_arguments.push_back(subscript->index);
		subscript->type_argument_is_nullable.push_back(match(BSTokenizer::Token::QUESTION_MARK));
		while (match(BSTokenizer::Token::COMMA) && !check(BSTokenizer::Token::BRACKET_CLOSE)) {
			ExpressionNode *type_argument = parse_expression(false, false, true);
			if (type_argument == nullptr) {
				push_error(R"(Expected type argument after ",".)");
				break;
			}
			subscript->type_arguments.push_back(type_argument);
			subscript->type_argument_is_nullable.push_back(match(BSTokenizer::Token::QUESTION_MARK));
		}
	}

	pop_multiline();
	consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected "]" after subscription index.)");
	complete_extents(subscript);

	return subscript;
}

BSParser::ExpressionNode *BSParser::parse_cast(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// D1: `as` is the only cast operator. Upstream also reaches here on `Token::AS_BANG`
	// (fs_parser.cpp:5748-5749 @ c9d5e35), which the tokenizer no longer produces.
	CastNode *cast = alloc_node<CastNode>();
	reset_extents(cast, p_previous_operand);
	update_extents(cast);

	cast->operand = p_previous_operand;
	cast->cast_type = parse_type();
	complete_extents(cast);

	if (cast->cast_type == nullptr) {
		push_error(R"(Expected type specifier after "as".)");
		return p_previous_operand;
	}

	return cast;
}

BSParser::ExpressionNode *BSParser::parse_call(ExpressionNode *p_previous_operand, bool p_can_assign) {
	CallNode *call = alloc_node<CallNode>();
	reset_extents(call, p_previous_operand);

	if (previous.type == BSTokenizer::Token::SUPER) {
		// Super call.
		call->is_super = true;
		if (!check(BSTokenizer::Token::PERIOD)) {
			make_completion_context(COMPLETION_SUPER, call);
		}
		push_multiline(true);
		if (match(BSTokenizer::Token::PARENTHESIS_OPEN)) {
			// Implicit call to the parent method of the same name.
			if (current_function == nullptr) {
				push_error(R"(Cannot use implicit "super" call outside of a function.)");
				pop_multiline();
				complete_extents(call);
				return nullptr;
			}
			if (current_function->identifier) {
				call->function_name = current_function->identifier->name;
			} else {
				call->function_name = SNAME("<anonymous>");
			}
		} else {
			consume(BSTokenizer::Token::PERIOD, R"(Expected "." or "(" after "super".)");
			make_completion_context(COMPLETION_SUPER_METHOD, call);
			if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected function name after ".".)")) {
				pop_multiline();
				complete_extents(call);
				return nullptr;
			}
			IdentifierNode *identifier = parse_identifier();
			call->callee = identifier;
			call->function_name = identifier->name;
			if (!consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after function name.)")) {
				pop_multiline();
				complete_extents(call);
				return nullptr;
			}
		}
	} else {
		call->callee = p_previous_operand;

		if (call->callee == nullptr) {
			push_error(R"*(Cannot call on an expression. Use ".call()" if it's a Callable.)*");
		} else if (call->callee->type == Node::IDENTIFIER) {
			call->function_name = static_cast<IdentifierNode *>(call->callee)->name;
			make_completion_context(COMPLETION_METHOD, call->callee);
		} else if (call->callee->type == Node::SUBSCRIPT) {
			SubscriptNode *attribute = static_cast<SubscriptNode *>(call->callee);
			if (attribute->is_attribute) {
				if (attribute->attribute) {
					call->function_name = attribute->attribute->name;
				}
				// `.Ok(1)`: the payload form of the contextual case shorthand is the leading-`.`
				// case reference with the ordinary call suffix applied to it. It has no base to
				// complete members against, so it stays out of the attribute completion context.
				call->is_contextual_enum_case = attribute->is_contextual_enum_case;
				if (!attribute->is_contextual_enum_case) {
					make_completion_context(COMPLETION_ATTRIBUTE_METHOD, call->callee);
				}
			} else {
				// `expr[...](...)`: either a generic method application (`name[TypeArgs](...)` or
				// `receiver.method[TypeArgs](...)`, where the brackets are a use-site type-argument
				// list) or an invalid call on an index. The parser cannot tell them apart, so it
				// builds the call and lets call reduction decide and diagnose (mirroring how
				// `Array[int]` is read during analysis). Record the method name for the analyzer.
				if (attribute->base != nullptr && attribute->base->type == Node::IDENTIFIER) {
					call->function_name = static_cast<IdentifierNode *>(attribute->base)->name;
					make_completion_context(COMPLETION_METHOD, attribute->base);
				} else if (attribute->base != nullptr && attribute->base->type == Node::SUBSCRIPT) {
					// `receiver.method[TypeArgs](...)`: the type-argument list is applied to a method
					// accessed as an attribute on a receiver.
					SubscriptNode *receiver_access = static_cast<SubscriptNode *>(attribute->base);
					if (receiver_access->is_attribute && receiver_access->attribute != nullptr) {
						call->function_name = receiver_access->attribute->name;
						make_completion_context(COMPLETION_ATTRIBUTE_METHOD, receiver_access);
					}
				}
			}
		} else {
			push_error(R"*(Cannot call on an expression. Use ".call()" if it's a Callable.)*");
		}
	}

	// Arguments.
	CompletionType ct = COMPLETION_CALL_ARGUMENTS;
	if (call->function_name == SNAME("load")) {
		ct = COMPLETION_RESOURCE_PATH;
	}
	push_completion_call(call);
	int argument_index = 0;
	do {
		make_completion_context(ct, call, argument_index);
		set_last_completion_call_arg(argument_index);
		if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
			// Allow for trailing comma.
			break;
		}
		// Stop on a trailing "=" so a leading identifier followed by "=" can be read as a
		// named argument (`name = value`). BaristaScript assignment is a statement, never an
		// expression, so `IDENTIFIER` + `EQUAL` here is unambiguously a named argument;
		// `f(a == b)` uses `EQUAL_EQUAL` and `f(a)` has no following "=".
		ExpressionNode *argument = parse_expression(false, true);
		StringName argument_name;
		if (argument != nullptr && check(BSTokenizer::Token::EQUAL)) {
			if (argument->type == Node::IDENTIFIER) {
				argument_name = static_cast<IdentifierNode *>(argument)->name;
				advance(); // Consume "=".
				make_completion_context(ct, call, argument_index);
				argument = parse_expression(false);
				if (argument == nullptr) {
					push_error(vformat(R"(Expected expression after "%s =" named argument.)", argument_name));
				}
			} else {
				// A non-identifier target before "=" is an attempted assignment, which is not
				// a valid expression argument. Consume the rest so the stream recovers cleanly.
				push_error(R"(Assignment is not allowed inside an expression.)");
				advance(); // Consume "=".
				parse_expression(false);
			}
		}
#ifdef TOOLS_ENABLED
		// Record the surface argument name for each slot for editor code completion, indexed
		// by slot so it stays aligned with the completion's current argument. This is captured
		// even when the value is still missing (e.g. the cursor sits right after `name =`),
		// which `argument_names` below cannot represent, and it survives the analyzer's later
		// canonicalization that clears `argument_names`.
		if (call->parsed_argument_names.size() <= argument_index) {
			call->parsed_argument_names.resize(argument_index + 1);
		}
		call->parsed_argument_names.write[argument_index] = argument_name;
#endif // TOOLS_ENABLED

		if (argument == nullptr) {
			if (argument_name == StringName()) {
				push_error(R"(Expected expression as the function argument.)");
			}
		} else {
			call->arguments.push_back(argument);
			call->argument_names.push_back(argument_name);

			if (argument->type == Node::LITERAL) {
				override_completion_context(argument, ct, call, argument_index);
			}
		}

		ct = COMPLETION_CALL_ARGUMENTS;
		argument_index++;
	} while (match(BSTokenizer::Token::COMMA));
	pop_completion_call();

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after call arguments.)*");
	complete_extents(call);

	return call;
}

BSParser::ExpressionNode *BSParser::parse_get_node(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// We want code completion after a DOLLAR even if the current code is invalid.
	make_completion_context(COMPLETION_GET_NODE, nullptr, -1);

	if (!current.is_node_name() && !check(BSTokenizer::Token::LITERAL) && !check(BSTokenizer::Token::SLASH) && !check(BSTokenizer::Token::PERCENT)) {
		push_error(vformat(R"(Expected node path as string or identifier after "%s".)", previous.get_name()));
		return nullptr;
	}

	if (check(BSTokenizer::Token::LITERAL)) {
		if (current.literal.get_type() != Variant::STRING) {
			push_error(vformat(R"(Expected node path as string or identifier after "%s".)", previous.get_name()));
			return nullptr;
		}
	}

	GetNodeNode *get_node = alloc_node<GetNodeNode>();

	// Store the last item in the path so the parser knows what to expect.
	// Allow allows more specific error messages.
	enum PathState {
		PATH_STATE_START,
		PATH_STATE_SLASH,
		PATH_STATE_PERCENT,
		PATH_STATE_NODE_NAME,
	} path_state = PATH_STATE_START;

	if (previous.type == BSTokenizer::Token::DOLLAR) {
		// Detect initial slash, which will be handled in the loop if it matches.
		match(BSTokenizer::Token::SLASH);
	} else {
		get_node->use_dollar = false;
	}

	int context_argument = 0;

	do {
		if (previous.type == BSTokenizer::Token::PERCENT) {
			if (path_state != PATH_STATE_START && path_state != PATH_STATE_SLASH) {
				push_error(R"("%" is only valid in the beginning of a node name (either after "$" or after "/"))");
				complete_extents(get_node);
				return nullptr;
			}

			get_node->full_path += "%";

			path_state = PATH_STATE_PERCENT;
		} else if (previous.type == BSTokenizer::Token::SLASH) {
			if (path_state != PATH_STATE_START && path_state != PATH_STATE_NODE_NAME) {
				push_error(R"("/" is only valid at the beginning of the path or after a node name.)");
				complete_extents(get_node);
				return nullptr;
			}

			get_node->full_path += "/";

			path_state = PATH_STATE_SLASH;
		}

		make_completion_context(COMPLETION_GET_NODE, get_node, context_argument++);

		if (match(BSTokenizer::Token::LITERAL)) {
			if (previous.literal.get_type() != Variant::STRING) {
				String previous_token;
				switch (path_state) {
					case PATH_STATE_START:
						previous_token = "$";
						break;
					case PATH_STATE_PERCENT:
						previous_token = "%";
						break;
					case PATH_STATE_SLASH:
						previous_token = "/";
						break;
					default:
						break;
				}
				push_error(vformat(R"(Expected node path as string or identifier after "%s".)", previous_token));
				complete_extents(get_node);
				return nullptr;
			}

			get_node->full_path += String(previous.literal);

			path_state = PATH_STATE_NODE_NAME;
		} else if (current.is_node_name()) {
			advance();

			String identifier = previous.get_identifier();
#ifdef DEBUG_ENABLED
			// Check spoofing.
			if (BSWarning::is_confusable_identifier(identifier)) {
				push_warning(get_node, BSWarning::CONFUSABLE_IDENTIFIER, identifier);
			}
#endif
			get_node->full_path += identifier;

			path_state = PATH_STATE_NODE_NAME;
		} else if (!check(BSTokenizer::Token::SLASH) && !check(BSTokenizer::Token::PERCENT)) {
			push_error(vformat(R"(Unexpected "%s" in node path.)", current.get_name()));
			complete_extents(get_node);
			return nullptr;
		}
	} while (match(BSTokenizer::Token::SLASH) || match(BSTokenizer::Token::PERCENT));

	complete_extents(get_node);
	return get_node;
}

BSParser::ExpressionNode *BSParser::parse_preload(ExpressionNode *p_previous_operand, bool p_can_assign) {
	PreloadNode *preload = alloc_node<PreloadNode>();
	preload->resolved_path = "<missing path>";

	push_multiline(true);
	consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected "(" after "preload".)");

	make_completion_context(COMPLETION_RESOURCE_PATH, preload);
	push_completion_call(preload);

	preload->path = parse_expression(false);

	if (preload->path == nullptr) {
		push_error(R"(Expected resource path after "(".)");
	} else if (preload->path->type == Node::LITERAL) {
		override_completion_context(preload->path, COMPLETION_RESOURCE_PATH, preload);
	}

	pop_completion_call();

	// Allow trailing comma.
	match(BSTokenizer::Token::COMMA);

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after preload path.)*");
	complete_extents(preload);

	return preload;
}

BSParser::ExpressionNode *BSParser::parse_lambda(ExpressionNode *p_previous_operand, bool p_can_assign) {
	LambdaNode *lambda = alloc_node<LambdaNode>();
	lambda->parent_function = current_function;
	lambda->parent_lambda = current_lambda;

	FunctionNode *function = alloc_node<FunctionNode>();
	function->source_lambda = lambda;

	function->is_static = current_function != nullptr ? current_function->is_static : false;

	if (match(BSTokenizer::Token::IDENTIFIER)) {
		function->identifier = parse_identifier();
	}

	bool multiline_context = multiline_stack.back()->get();

	push_completion_call(nullptr);

	// Reset the multiline stack since we don't want the multiline mode one in the lambda body.
	push_multiline(false);
	if (multiline_context) {
		tokenizer->push_expression_indented_block();
	}

	push_multiline(true); // For the parameters.
	if (function->identifier) {
		consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected opening "(" after lambda name.)");
	} else {
		consume(BSTokenizer::Token::PARENTHESIS_OPEN, R"(Expected opening "(" after "func".)");
	}

	FunctionNode *previous_function = current_function;
	current_function = function;

	LambdaNode *previous_lambda = current_lambda;
	current_lambda = lambda;

	SuiteNode *body = alloc_node<SuiteNode>();
	body->parent_function = current_function;
	body->parent_block = current_suite;

	SuiteNode *previous_suite = current_suite;
	current_suite = body;

	// A lambda's parameters and body are their own scope, parsed independently of any condition
	// that happens to contain this lambda expression: a case-bind test written inside them is not
	// an `and`-conjunct of that outer condition and has no matching declare_condition_case_binds()
	// call to clean it up. Suppress transient case-bind declaration for the whole lambda.
	int previous_case_bind_condition_depth = case_bind_condition_depth;
	case_bind_condition_depth = 0;

	parse_function_signature(function, body, "lambda", -1);

	current_suite = previous_suite;

	bool previous_in_lambda = in_lambda;
	in_lambda = true;

	// Save break/continue state.
	bool could_break = can_break;
	bool could_continue = can_continue;

	// Disallow break/continue.
	can_break = false;
	can_continue = false;

	function->body = parse_suite("lambda declaration", body, true);
	complete_extents(function);
	complete_extents(lambda);

	case_bind_condition_depth = previous_case_bind_condition_depth;

	pop_multiline();

	pop_completion_call();

	if (multiline_context) {
		// If we're in multiline mode, we want to skip the spurious DEDENT and NEWLINE tokens.
		while (check(BSTokenizer::Token::DEDENT) || check(BSTokenizer::Token::INDENT) || check(BSTokenizer::Token::NEWLINE)) {
			current = tokenizer->scan(); // Not advance() since we don't want to change the previous token.
		}
		tokenizer->pop_expression_indented_block();
	}

	current_function = previous_function;
	current_lambda = previous_lambda;
	in_lambda = previous_in_lambda;
	lambda->function = function;

	// Reset break/continue state.
	can_break = could_break;
	can_continue = could_continue;

	return lambda;
}

BSParser::ExpressionNode *BSParser::parse_type_test(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// x is not int
	// ^        ^^^ ExpressionNode, TypeNode
	// ^^^^^^^^^^^^ TypeTestNode
	// ^^^^^^^^^^^^ UnaryOpNode
	UnaryOpNode *not_node = nullptr;
	if (match(BSTokenizer::Token::NOT)) {
		not_node = alloc_node<UnaryOpNode>();
		not_node->operation = UnaryOpNode::OP_LOGIC_NOT;
		not_node->variant_op = Variant::OP_NOT;
		not_node->source_is_not = true;
		reset_extents(not_node, p_previous_operand);
		update_extents(not_node);
	}

	TypeTestNode *type_test = alloc_node<TypeTestNode>();
	reset_extents(type_test, p_previous_operand);
	update_extents(type_test);

	type_test->operand = p_previous_operand;
	// `x is .Ok(value)`: the contextual shorthand names a case of the operand's own union, so the
	// test type is the case name alone and the union is supplied by the analyzer.
	// A bare `.` with no name yet is still the shorthand: routing it here keeps the case-completion
	// context and reports the missing name once, instead of falling back to a written-out type.
	if (check(BSTokenizer::Token::PERIOD)) {
		type_test->test_type = parse_contextual_enum_case_type();
	} else {
		type_test->test_type = parse_type(false, COMPLETION_NONE, true);
	}
	if (type_test->test_type != nullptr) {
		if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
			parse_type_test_case_binds(type_test);
		}
	}
	complete_extents(type_test);

	if (not_node != nullptr) {
		not_node->operand = type_test;
		complete_extents(not_node);
		if (!type_test->case_binds.is_empty()) {
			push_error(R"(Cannot bind case payloads with "is not".)", type_test);
		}
	}

	if (type_test->test_type == nullptr) {
		if (not_node == nullptr) {
			push_error(R"(Expected type specifier after "is".)");
		} else {
			push_error(R"(Expected type specifier after "is not".)");
		}
	}

	if (not_node != nullptr) {
		return not_node;
	}

	return type_test;
}

void BSParser::parse_type_test_case_binds(TypeTestNode *p_type_test) {
	push_multiline(true);
	advance(); // Consume "(".

	if (match(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
		pop_multiline();
		push_error(R"(Expected at least one case payload bind name or "_" after "(".)");
		return;
	}

	HashSet<StringName> seen_names;
	do {
		if (is_at_end() || check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
			break;
		}
		if (match(BSTokenizer::Token::UNDERSCORE)) {
			// A skipped payload field still occupies a position, so record it as an empty bind.
			p_type_test->case_binds.push_back(nullptr);
			continue;
		}
		if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected case payload bind name or "_".)")) {
			break;
		}
		IdentifierNode *bind = parse_identifier();
		if (bind == nullptr) {
			break;
		}
		if (seen_names.has(bind->name)) {
			push_error(vformat(R"(Bind name "%s" was already used in this case test.)", bind->name), bind);
		} else {
			seen_names.insert(bind->name);
			declare_transient_case_bind(bind);
		}
		p_type_test->case_binds.push_back(bind);
	} while (match(BSTokenizer::Token::COMMA));

	pop_multiline();
	consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected ")" after case payload binds.)*");
}

void BSParser::declare_transient_case_bind(IdentifierNode *p_bind) {
	if (current_suite == nullptr || case_bind_condition_depth == 0) {
		// A case-bind test parsed outside any suite (e.g. a class-level member initializer), or
		// outside the condition of an if/elif/while/assert (e.g. an ordinary expression statement),
		// has no matching declare_condition_case_binds() call downstream to remove a transient local.
		// Leave it undeclared; the analyzer still rejects it via TypeTestNode::binds_allowed.
		return;
	}
	if (current_suite->has_local(p_bind->name)) {
		const SuiteNode::Local &existing = current_suite->get_local(p_bind->name);
		push_error(vformat(R"(There's already a %s named "%s" in this scope.)", existing.get_name(), p_bind->name), p_bind);
		return;
	}
	SuiteNode::Local local(p_bind, current_function);
	local.type = SuiteNode::Local::CASE_BIND;
	current_suite->add_local(local);
	pending_case_binds.push_back(p_bind);
}

void BSParser::collect_condition_case_binds(ExpressionNode *p_condition, Vector<TypeTestNode *> &r_type_tests) {
	if (p_condition == nullptr) {
		return;
	}

	if (p_condition->type == Node::BINARY_OPERATOR) {
		BinaryOpNode *binary_op = static_cast<BinaryOpNode *>(p_condition);
		if (binary_op->variant_op == Variant::OP_AND) {
			collect_condition_case_binds(binary_op->left_operand, r_type_tests);
			collect_condition_case_binds(binary_op->right_operand, r_type_tests);
		}
		return;
	}

	if (p_condition->type != Node::TYPE_TEST) {
		return;
	}

	TypeTestNode *type_test = static_cast<TypeTestNode *>(p_condition);
	type_test->binds_allowed = true;
	if (!type_test->case_binds.is_empty()) {
		r_type_tests.push_back(type_test);
	}
}

bool BSParser::declare_condition_case_binds(const Vector<TypeTestNode *> &p_type_tests, SuiteNode *p_suite, int p_pending_mark) {
	// Every bind parsed since p_pending_mark was declared into current_suite as a transient local so
	// that later `and`-conjuncts of this same condition could already reference it (see
	// declare_transient_case_bind()). Now that the whole condition has been parsed, drop those
	// transient entries...
	HashSet<StringName> transient_names;
	for (int i = p_pending_mark; i < pending_case_binds.size(); i++) {
		transient_names.insert(pending_case_binds[i]->name);
	}
	if (!transient_names.is_empty()) {
		Vector<SuiteNode::Local> kept_locals;
		kept_locals.reserve(current_suite->locals.size());
		for (const SuiteNode::Local &local : current_suite->locals) {
			if (!transient_names.has(local.name)) {
				kept_locals.push_back(local);
			}
		}
		current_suite->locals = kept_locals;
		current_suite->locals_indices.clear();
		for (int i = 0; i < current_suite->locals.size(); i++) {
			current_suite->locals_indices[current_suite->locals[i].name] = i;
		}
	}

	// ...and relocate the ones that ended up in a legal bind position (i.e. collected into
	// p_type_tests, directly or as an `and`-conjunct) into the guarded suite. Binds that never made
	// it past declare_transient_case_bind() (name conflicts) and binds in an illegal position (not
	// present in p_type_tests) are left undeclared, exactly as if they had never been written.
	HashSet<IdentifierNode *> legal_binds;
	for (TypeTestNode *type_test : p_type_tests) {
		for (IdentifierNode *bind : type_test->case_binds) {
			if (bind != nullptr) {
				legal_binds.insert(bind);
			}
		}
	}

	bool declared_any = false;
	for (int i = p_pending_mark; i < pending_case_binds.size(); i++) {
		IdentifierNode *bind = pending_case_binds[i];
		if (!legal_binds.has(bind)) {
			continue;
		}
		SuiteNode::Local local(bind, current_function);
		local.type = SuiteNode::Local::CASE_BIND;
		p_suite->add_local(local);
		declared_any = true;
	}

	pending_case_binds.resize(p_pending_mark);
	return declared_any;
}

BSParser::ExpressionNode *BSParser::parse_yield(ExpressionNode *p_previous_operand, bool p_can_assign) {
	push_error(R"("yield" was removed in Godot 4. Use "await" instead.)");
	return nullptr;
}

BSParser::ExpressionNode *BSParser::parse_invalid_token(ExpressionNode *p_previous_operand, bool p_can_assign) {
	// Just for better error messages.
	BSTokenizer::Token::Type invalid = previous.type;

	switch (invalid) {
		case BSTokenizer::Token::QUESTION_MARK:
			push_error(R"(Unexpected "?" in source. If you want a ternary operator, use "truthy_value if true_condition else falsy_value".)");
			break;
		default:
			return nullptr; // Unreachable.
	}

	// Return the previous expression.
	return p_previous_operand;
}

// True when the token stream at a declaration position starts a `type Name = ...` alias. `type` is
// an ordinary identifier everywhere else, so the decision is exactly this two-token lookahead: no
// other construct spells an identifier `type` immediately followed by another identifier, and no
// declaration position admits an expression statement, so nothing valid is stolen.
bool BSParser::starts_type_alias_declaration() {
	if (current.type != BSTokenizer::Token::IDENTIFIER || current.get_identifier() != StringName("type")) {
		return false;
	}
	return peek().is_identifier();
}

BSParser::TypeAliasNode *BSParser::parse_type_alias() {
	TypeAliasNode *type_alias = alloc_node<TypeAliasNode>();

	// The current token is the contextual `type` identifier. `alloc_node` anchored the node to the
	// preceding token, so re-anchor it to `type` itself once consumed, keeping diagnostics on the
	// declaration.
	advance();
	reset_extents(type_alias, previous);

	// An alias is erased before runtime and declares no member, so no annotation may apply to it.
	// Consume any pending annotations here (erroring on each) so they cannot silently carry over
	// onto the next real member.
	parse_class_member_annotations(AnnotationInfo::NONE, "type alias");

	reject_reserved_type_name();
	if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected an alias name after "type".)")) {
		complete_extents(type_alias);
		return nullptr;
	}
	type_alias->identifier = parse_identifier();

	if (check(BSTokenizer::Token::BRACKET_OPEN)) {
		push_error(R"(A type alias cannot declare type parameters.)");
		// Consume the parameter list so the rest of the declaration still parses.
		Vector<TypeParameterNode *> discarded_parameters;
		parse_type_parameters(discarded_parameters);
	}

	if (!consume(BSTokenizer::Token::EQUAL, R"(Expected "=" after the type alias name.)")) {
		complete_extents(type_alias);
		return nullptr;
	}

	type_alias->aliased_type = parse_type();
	if (type_alias->aliased_type == nullptr) {
		push_error(R"(Expected a type after "=" in a type alias declaration.)");
	}

	complete_extents(type_alias);
	end_statement("type alias declaration");
	return type_alias;
}

// Rejects the members a union may never contain and that are recognizable from syntax alone. `void`
// is not a value type at all, and `Variant` absorbs every alternative, so a union naming it means
// exactly `Variant` and is written that way instead. Members whose invalidity only shows after
// resolution are rejected by the analyzer.
void BSParser::validate_union_member_type(const TypeNode *p_member) {
	if (p_member == nullptr) {
		return;
	}
	if (!p_member->is_tuple && !p_member->is_union && p_member->type_chain.is_empty()) {
		push_error(R"("void" cannot be a member of a type union.)", p_member);
		return;
	}
	const bool is_bare_name = p_member->type_chain.size() == 1 && !p_member->has_signature &&
			p_member->container_types.is_empty() && p_member->type_argument_expressions.is_empty();
	if (!is_bare_name) {
		return;
	}
	const StringName member_name = p_member->type_chain[0]->name;
	if (member_name == SNAME("Variant")) {
		push_error(R"("Variant" cannot be a member of a type union, since it already admits every type. Write "Variant" on its own instead.)", p_member);
		return;
	}
	if (is_enclosing_type_parameter_name(member_name)) {
		// A type parameter is erased, so a union that names one has no static meaning: nothing
		// could satisfy the alternative that is not already covered by the parameter's own bound.
		// A parameter still nests freely inside a member (`Array[T] | int`).
		push_error(vformat(R"(Type parameter "%s" cannot be a member of a type union.)", member_name), p_member);
	}
}

// True when p_name is a type parameter of the function or of a class currently being parsed. The
// same shadowing rule the analyzer resolves types with applies here: an enclosing parameter of that
// name wins over every other meaning, so this identifies exactly the bare-type-parameter members a
// union may not contain.
bool BSParser::is_enclosing_type_parameter_name(const StringName &p_name) const {
	// A lambda body is parsed with the lambda's own synthetic function current, so the walk follows
	// the lambda chain back to the declaration that owns the type parameters.
	for (const FunctionNode *scope = current_function; scope != nullptr;
			scope = scope->source_lambda != nullptr ? scope->source_lambda->parent_function : nullptr) {
		for (const TypeParameterNode *type_parameter : scope->type_parameters) {
			if (type_parameter->identifier != nullptr && type_parameter->identifier->name == p_name) {
				return true;
			}
		}
	}
	if (current_enum != nullptr) {
		for (const TypeParameterNode *type_parameter : current_enum->type_parameters) {
			if (type_parameter->identifier != nullptr && type_parameter->identifier->name == p_name) {
				return true;
			}
		}
	}
	for (const ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
		for (const TypeParameterNode *type_parameter : scope->type_parameters) {
			if (type_parameter->identifier != nullptr && type_parameter->identifier->name == p_name) {
				return true;
			}
		}
	}
	return false;
}

BSParser::TypeNode *BSParser::parse_type(bool p_allow_void, CompletionType p_forced_completion, bool p_allow_enum_case) {
	TypeNode *first_member = parse_type_member(p_allow_void, p_forced_completion, p_allow_enum_case);
	if (first_member == nullptr || !check(BSTokenizer::Token::PIPE)) {
		return first_member;
	}

	// `|` unions types only while a type is being parsed, and there it binds looser than every
	// other type operator, `?` included: `int? | uint` is the union of a nullable `int` and a
	// `uint`, never a nullable union. There is no parenthesized type form to say otherwise --
	// `(A, B)` is already an unnamed tuple -- so nullability is hoisted during normalization
	// instead. Expression-level `|` keeps its own precedence and meaning as bitwise OR.
	TypeNode *union_type = alloc_node<TypeNode>();
	reset_extents(union_type, first_member);
	union_type->is_union = true;
	union_type->allows_enum_case = p_allow_enum_case;
	union_type->union_member_types.push_back(first_member);

	while (match(BSTokenizer::Token::PIPE)) {
		TypeNode *member = parse_type_member(false, p_forced_completion, p_allow_enum_case);
		if (member == nullptr) {
			push_error(R"(Expected a type after "|".)");
			break;
		}
		union_type->union_member_types.push_back(member);
	}

	for (const TypeNode *member : union_type->union_member_types) {
		validate_union_member_type(member);
	}

	complete_extents(union_type);
	return union_type;
}

BSParser::TypeNode *BSParser::parse_type_member(bool p_allow_void, CompletionType p_forced_completion, bool p_allow_enum_case) {
	// Nested type annotations (e.g. `Array[Array[...]]`, Callable/Coroutine signatures)
	// recurse through parse_type(); bound that depth so a pathologically nested type
	// reports an error instead of overflowing the native stack.
	RecursionDepthGuard depth_guard(type_nesting_depth);
	if (unlikely(type_nesting_depth > MAX_NESTING_DEPTH)) {
		push_error("Type nesting is too deep.");
		return nullptr;
	}

	TypeNode *type = alloc_node<TypeNode>();
	type->allows_enum_case = p_allow_enum_case;
	if (p_forced_completion != COMPLETION_NONE) {
		make_completion_context(p_forced_completion, type);
	} else {
		make_completion_context(p_allow_void ? COMPLETION_TYPE_NAME_OR_VOID : COMPLETION_TYPE_NAME, type);
	}

	// Unnamed tuple type: `(int, String)`. Structural, arity >= 2; `type_chain` stays empty.
	if (check(BSTokenizer::Token::PARENTHESIS_OPEN)) {
		advance();
		push_multiline(true);
		type->is_tuple = true;

		if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
			push_error(R"(A tuple type cannot be empty.)");
		} else {
			do {
				if (check(BSTokenizer::Token::PARENTHESIS_CLOSE)) {
					break; // Allow for trailing comma.
				}
				TypeNode *element_type = parse_type(false);
				if (element_type == nullptr) {
					push_error(R"(Expected a tuple element type.)");
					break;
				}
				type->tuple_element_types.push_back(element_type);
			} while (match(BSTokenizer::Token::COMMA));
		}

		pop_multiline();
		consume(BSTokenizer::Token::PARENTHESIS_CLOSE, R"*(Expected closing ")" after tuple type.)*");

		if (type->tuple_element_types.size() == 1) {
			push_error(R"(A tuple type must have at least two element types.)");
		}
		if (match(BSTokenizer::Token::QUESTION_MARK)) {
			type->is_nullable = true;
		}
		complete_extents(type);
		return type;
	}

	reject_reserved_type_name();

	if (!match(BSTokenizer::Token::IDENTIFIER)) {
		if (match(BSTokenizer::Token::TK_VOID)) {
			if (p_allow_void) {
				complete_extents(type);
				TypeNode *void_type = type;
				return void_type;
			} else {
				push_error(R"("void" is only allowed for a function return type.)");
			}
		}
		// Leave error message to the caller who knows the context.
		complete_extents(type);
		return nullptr;
	}

	IdentifierNode *type_element = parse_identifier();

	type->type_chain.push_back(type_element);

	// AsyncCallable mirrors Callable typing but carries an async marker for structured concurrency.
	if (type->type_chain.size() == 1 && type_element->name == SNAME("AsyncCallable")) {
		type->signature_is_async = true;
	}

	// A type-argument list binds to the last name of the chain, so the same loop serves both the
	// unqualified head (`Box[int]`) and the qualified one (`Outer.Box[int]`). `type` is cleared when an
	// argument fails to parse, which the callers below turn into a null return.
	auto parse_collection_type_arguments = [&]() {
		type->type_arguments_chain_index = type->type_chain.size() - 1;
		bool first_pass = true;
		do {
			TypeNode *container_type = parse_type(false); // Don't allow void for element type.
			if (container_type == nullptr) {
				push_error(vformat(R"(Expected type for collection after "%s".)", first_pass ? "[" : ","));
				complete_extents(type);
				type = nullptr;
				break;
			} else {
				type->container_types.append(container_type);
			}
			first_pass = false;
		} while (match(BSTokenizer::Token::COMMA));
		consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after collection type.)");
	};

	bool parsed_type_arguments = false;

	if (match(BSTokenizer::Token::BRACKET_OPEN)) {
		const bool is_type_handle = type->type_chain.size() == 1 && type_element->name == SNAME("Type");
		const bool is_callable_type = type->type_chain.size() == 1 && (type_element->name == SNAME("Callable") || type_element->name == SNAME("AsyncCallable"));
		const bool is_signal_type = type->type_chain.size() == 1 && type_element->name == SNAME("Signal");
		// Coroutine[T] is the typed handle to an in-flight async computation for structured concurrency.
		const bool is_coroutine_type = type->type_chain.size() == 1 && type_element->name == SNAME("Coroutine");
		if ((is_callable_type || is_signal_type) && match(BSTokenizer::Token::BRACKET_OPEN)) {
			type->has_signature = true;
			if (!check(BSTokenizer::Token::BRACKET_CLOSE)) {
				bool first_pass = true;
				// Count of fixed parameter types seen when the rest entry was parsed. A later fixed entry
				// means the rest entry was not final.
				int rest_parameter_position = -1;
				do {
					if (match(BSTokenizer::Token::PERIOD_PERIOD_PERIOD)) {
						if (is_signal_type) {
							push_error("Signal signatures cannot declare a rest parameter.");
						} else if (rest_parameter_position >= 0) {
							push_error("A Callable signature can contain only one rest parameter type.");
						}
						TypeNode *rest_type = parse_type(false);
						if (rest_type == nullptr) {
							push_error(R"(Expected rest parameter type for signature after "...".)");
							break;
						}
						// Keep the first rest entry so a duplicated or Signal spelling still recovers with a
						// usable signature while retaining the original error.
						if (rest_parameter_position < 0 && !is_signal_type) {
							type->signature_rest_parameter_type = rest_type;
							rest_parameter_position = type->signature_parameter_types.size();
						}
						first_pass = false;
						continue;
					}
					TypeNode *parameter_type = parse_type(false);
					if (parameter_type == nullptr) {
						push_error(vformat(R"(Expected parameter type for signature after "%s".)", first_pass ? "[" : ","));
						break;
					}
					type->signature_parameter_types.append(parameter_type);
					if (rest_parameter_position >= 0 && type->signature_parameter_types.size() == rest_parameter_position + 1) {
						push_error("The rest parameter type must be the final Callable parameter type.");
					}
					first_pass = false;
				} while (match(BSTokenizer::Token::COMMA) && !check(BSTokenizer::Token::BRACKET_CLOSE));
			}
			consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after signature parameter types.)");
			if (is_callable_type) {
				// Gate return-type parsing on the required separator. Speculatively parsing a return
				// type after a malformed/unclosed parameter list would overwrite the parameter's
				// COMPLETION_TYPE_NAME with COMPLETION_TYPE_NAME_OR_VOID at the same cursor.
				if (consume(BSTokenizer::Token::COMMA, R"(Expected "," after Callable signature parameter types.)")) {
					type->signature_return_type = parse_type(true);
					if (type->signature_return_type == nullptr) {
						push_error("Expected return type for Callable signature.");
					}
				}
			} else if (match(BSTokenizer::Token::COMMA)) {
				push_error("Signal signatures cannot specify a return type.");
				parse_type(true);
			}
			consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after signature type.)");
			if (match(BSTokenizer::Token::QUESTION_MARK)) {
				type->is_nullable = true;
			}
			complete_extents(type);
			return type;
		}

		// Coroutine[T] carries exactly one phantom result type, unlike the variadic typed collections.
		if (is_coroutine_type) {
			type->is_coroutine = true;
			if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
				push_error(R"(Coroutine[T] expects a single result type parameter.)");
				// Still enter parse_type so Coroutine[<caret>] records COMPLETION_TYPE_NAME_OR_VOID.
				parse_type(true);
			} else {
				TypeNode *result_type = parse_type(true); // Allow void so void-returning async work is nameable as Coroutine[void].
				if (result_type == nullptr) {
					push_error(R"(Expected result type for Coroutine after "[".)");
				} else {
					type->container_types.append(result_type);
				}
				if (check(BSTokenizer::Token::COMMA)) {
					push_error(R"(Coroutine[T] expects a single result type parameter, but more were given.)");
					while (match(BSTokenizer::Token::COMMA)) {
						parse_type(false); // Consume the extra parameters so parsing stays aligned.
					}
				}
			}
			consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after Coroutine result type.)");
			if (match(BSTokenizer::Token::QUESTION_MARK)) {
				type->is_nullable = true;
			}
			complete_extents(type);
			return type;
		}

		// Type[T] carries exactly one represented instance type.
		if (is_type_handle) {
			if (check(BSTokenizer::Token::BRACKET_CLOSE)) {
				push_error(R"(Type[T] expects exactly one type argument.)");
				// Still enter parse_type so Type[<caret>] records COMPLETION_TYPE_HANDLE_ARGUMENT.
				parse_type(false, COMPLETION_TYPE_HANDLE_ARGUMENT);
			} else {
				TypeNode *represented_type = parse_type(false, COMPLETION_TYPE_HANDLE_ARGUMENT);
				if (represented_type == nullptr) {
					push_error(R"(Expected represented instance type for Type after "[".)");
				} else {
					type->container_types.append(represented_type);
				}
				if (check(BSTokenizer::Token::COMMA)) {
					push_error(R"(Type[T] expects exactly one type argument, but more were given.)");
					while (match(BSTokenizer::Token::COMMA)) {
						parse_type(false); // Consume the extra parameters so parsing stays aligned.
					}
				}
			}
			consume(BSTokenizer::Token::BRACKET_CLOSE, R"(Expected closing "]" after Type argument.)");
			if (match(BSTokenizer::Token::QUESTION_MARK)) {
				type->is_nullable = true;
			}
			complete_extents(type);
			return type;
		}

		// Typed collection (like Array[int], Dictionary[String, int]).
		parse_collection_type_arguments();
		if (type == nullptr) {
			return nullptr;
		}
		parsed_type_arguments = true;
		// Fall through to the shared trailing chain below: an applied generic tagged union names one
		// of its cases after its argument list (`Result[int, String].Ok`) wherever a case reference is
		// admitted, and the nullable marker is handled there for both spellings alike.
	}

	int chain_index = 1;
	bool reported_suffix_position = false;
	while (match(BSTokenizer::Token::PERIOD)) {
		// A name after an argument list is a tagged-union case, so it is only a type where the caller
		// asked for one. Everywhere else the arguments were written on the wrong chain element.
		if (parsed_type_arguments && !p_allow_enum_case) {
			if (!reported_suffix_position) {
				push_error(R"(A type-argument list must be written after the last name of a qualified type.)", type);
				reported_suffix_position = true;
			}
			// Consume the misplaced tail so parsing stays aligned, but keep it out of the chain: the
			// applied head is the type the author meant, and resolving the tail against it would only
			// add a derived "not a nested type" complaint after the real error.
			reject_reserved_type_name();
			if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected inner type name after ".".)")) {
				break;
			}
			parse_identifier();
			continue;
		}
		make_completion_context(COMPLETION_TYPE_ATTRIBUTE, type, chain_index++);
		reject_reserved_type_name();
		if (!consume(BSTokenizer::Token::IDENTIFIER, R"(Expected inner type name after ".".)")) {
			continue;
		}
		type_element = parse_identifier();
		type->type_chain.push_back(type_element);

		if (!parsed_type_arguments && match(BSTokenizer::Token::BRACKET_OPEN)) {
			// Only the generic/typed-collection argument list is reachable after a dotted chain; the
			// built-in signature forms above stay unqualified.
			parse_collection_type_arguments();
			if (type == nullptr) {
				return nullptr;
			}
			parsed_type_arguments = true;
		}
	}

	if (parsed_type_arguments && check(BSTokenizer::Token::BRACKET_OPEN) && !reported_suffix_position) {
		push_error(R"(A type can carry only one type-argument list, written after the last name of a qualified type.)", type);
	}

	if (match(BSTokenizer::Token::QUESTION_MARK)) {
		type->is_nullable = true;
	}
	complete_extents(type);
	return type;
}

#ifdef TOOLS_ENABLED
enum DocLineState {
	DOC_LINE_NORMAL,
	DOC_LINE_IN_CODE,
	DOC_LINE_IN_CODEBLOCK,
	DOC_LINE_IN_KBD,
};

static String _process_doc_line(const String &p_line, const String &p_text, const String &p_space_prefix, DocLineState &r_state) {
	String line = p_line;
	if (r_state == DOC_LINE_NORMAL) {
		line = line.strip_edges(true, false);
	} else {
		line = line.trim_prefix(p_space_prefix);
	}

	String line_join;
	if (!p_text.is_empty()) {
		if (r_state == DOC_LINE_NORMAL) {
			if (p_text.ends_with("[/codeblock]")) {
				line_join = "\n";
			} else if (!p_text.ends_with("[br]")) {
				line_join = " ";
			}
		} else {
			line_join = "\n";
		}
	}

	String result;
	int from = 0;
	int buffer_start = 0;
	const int len = line.length();
	bool process = true;
	while (process) {
		switch (r_state) {
			case DOC_LINE_NORMAL: {
				int lb_pos = line.find_char('[', from);
				if (lb_pos < 0) {
					process = false;
					break;
				}
				int rb_pos = line.find_char(']', lb_pos + 1);
				if (rb_pos < 0) {
					process = false;
					break;
				}

				from = rb_pos + 1;

				String tag = line.substr(lb_pos + 1, rb_pos - lb_pos - 1);
				if (tag == "code" || tag.begins_with("code ")) {
					r_state = DOC_LINE_IN_CODE;
				} else if (tag == "codeblock" || tag.begins_with("codeblock ")) {
					if (lb_pos == 0) {
						line_join = "\n";
					} else {
						result += line.substr(buffer_start, lb_pos - buffer_start) + '\n';
					}
					result += "[" + tag + "]";
					if (from < len) {
						result += '\n';
					}

					r_state = DOC_LINE_IN_CODEBLOCK;
					buffer_start = from;
				} else if (tag == "kbd") {
					r_state = DOC_LINE_IN_KBD;
				}
			} break;
			case DOC_LINE_IN_CODE: {
				int pos = line.find("[/code]", from);
				if (pos < 0) {
					process = false;
					break;
				}

				from = pos + 7; // `len("[/code]")`.

				r_state = DOC_LINE_NORMAL;
			} break;
			case DOC_LINE_IN_CODEBLOCK: {
				int pos = line.find("[/codeblock]", from);
				if (pos < 0) {
					process = false;
					break;
				}

				from = pos + 12; // `len("[/codeblock]")`.

				if (pos == 0) {
					line_join = "\n";
				} else {
					result += line.substr(buffer_start, pos - buffer_start) + '\n';
				}
				result += "[/codeblock]";
				if (from < len) {
					result += '\n';
				}

				r_state = DOC_LINE_NORMAL;
				buffer_start = from;
			} break;
			case DOC_LINE_IN_KBD: {
				int pos = line.find("[/kbd]", from);
				if (pos < 0) {
					process = false;
					break;
				}

				from = pos + 6; // `len("[/kbd]")`.

				r_state = DOC_LINE_NORMAL;
			} break;
		}
	}

	result += line.substr(buffer_start);
	if (r_state == DOC_LINE_NORMAL) {
		result = result.strip_edges(false, true);
	}

	return line_join + result;
}

bool BSParser::has_comment(int p_line, bool p_must_be_doc) {
	bool has_comment = tokenizer->get_comments().has(p_line);
	// If there are no comments or if we don't care whether the comment
	// is a docstring, we have our result.
	if (!p_must_be_doc || !has_comment) {
		return has_comment;
	}

	return tokenizer->get_comments()[p_line].comment.begins_with("##");
}

BSParser::MemberDocData BSParser::parse_doc_comment(int p_line, bool p_single_line) {
	ERR_FAIL_COND_V(!has_comment(p_line, true), MemberDocData());

	const HashMap<int, BSTokenizer::CommentData> &comments = tokenizer->get_comments();
	int line = p_line;

	if (!p_single_line) {
		while (comments.has(line - 1) && comments[line - 1].new_line && comments[line - 1].comment.begins_with("##")) {
			line--;
		}
	}

	max_script_doc_line = MIN(max_script_doc_line, line - 1);

	String space_prefix;
	{
		int i = 2;
		for (; i < comments[line].comment.length(); i++) {
			if (comments[line].comment[i] != ' ') {
				break;
			}
		}
		space_prefix = String(" ").repeat(i - 2);
	}

	DocLineState state = DOC_LINE_NORMAL;
	MemberDocData result;

	while (line <= p_line) {
		String doc_line = comments[line].comment.trim_prefix("##");
		line++;

		if (state == DOC_LINE_NORMAL) {
			String stripped_line = doc_line.strip_edges();
			if (stripped_line == "@deprecated" || stripped_line.begins_with("@deprecated:")) {
				result.is_deprecated = true;
				if (stripped_line.begins_with("@deprecated:")) {
					result.deprecated_message = stripped_line.trim_prefix("@deprecated:").strip_edges();
				}
				continue;
			} else if (stripped_line == "@experimental" || stripped_line.begins_with("@experimental:")) {
				result.is_experimental = true;
				if (stripped_line.begins_with("@experimental:")) {
					result.experimental_message = stripped_line.trim_prefix("@experimental:").strip_edges();
				}
				continue;
			}
		}

		result.description += _process_doc_line(doc_line, result.description, space_prefix, state);
	}

	return result;
}

BSParser::ClassDocData BSParser::parse_class_doc_comment(int p_line, bool p_single_line) {
	ERR_FAIL_COND_V(!has_comment(p_line, true), ClassDocData());

	const HashMap<int, BSTokenizer::CommentData> &comments = tokenizer->get_comments();
	int line = p_line;

	if (!p_single_line) {
		while (comments.has(line - 1) && comments[line - 1].new_line && comments[line - 1].comment.begins_with("##")) {
			line--;
		}
	}

	max_script_doc_line = MIN(max_script_doc_line, line - 1);

	String space_prefix;
	{
		int i = 2;
		for (; i < comments[line].comment.length(); i++) {
			if (comments[line].comment[i] != ' ') {
				break;
			}
		}
		space_prefix = String(" ").repeat(i - 2);
	}

	DocLineState state = DOC_LINE_NORMAL;
	bool is_in_brief = true;
	ClassDocData result;

	while (line <= p_line) {
		String doc_line = comments[line].comment.trim_prefix("##");
		line++;

		if (state == DOC_LINE_NORMAL) {
			String stripped_line = doc_line.strip_edges();

			// A blank line separates the description from the brief.
			if (is_in_brief && !result.brief.is_empty() && stripped_line.is_empty()) {
				is_in_brief = false;
				continue;
			}

			if (stripped_line.begins_with("@tutorial")) {
				String title, link;

				int begin_scan = String("@tutorial").length();
				if (begin_scan >= stripped_line.length()) {
					continue; // Invalid syntax.
				}

				if (stripped_line[begin_scan] == ':') { // No title.
					// Syntax: ## @tutorial: https://godotengine.org/ // The title argument is optional.
					title = "";
					link = stripped_line.trim_prefix("@tutorial:").strip_edges();
				} else {
					/* Syntax:
					 *   @tutorial ( The Title Here )         :         https://the.url/
					 *             ^ open           ^ close   ^ colon   ^ url
					 */
					int open_bracket_pos = begin_scan, close_bracket_pos = 0;
					while (open_bracket_pos < stripped_line.length() && (stripped_line[open_bracket_pos] == ' ' || stripped_line[open_bracket_pos] == '\t')) {
						open_bracket_pos++;
					}
					if (open_bracket_pos == stripped_line.length() || stripped_line[open_bracket_pos++] != '(') {
						continue; // Invalid syntax.
					}
					close_bracket_pos = open_bracket_pos;
					while (close_bracket_pos < stripped_line.length() && stripped_line[close_bracket_pos] != ')') {
						close_bracket_pos++;
					}
					if (close_bracket_pos == stripped_line.length()) {
						continue; // Invalid syntax.
					}

					int colon_pos = close_bracket_pos + 1;
					while (colon_pos < stripped_line.length() && (stripped_line[colon_pos] == ' ' || stripped_line[colon_pos] == '\t')) {
						colon_pos++;
					}
					if (colon_pos == stripped_line.length() || stripped_line[colon_pos++] != ':') {
						continue; // Invalid syntax.
					}

					title = stripped_line.substr(open_bracket_pos, close_bracket_pos - open_bracket_pos).strip_edges();
					link = stripped_line.substr(colon_pos).strip_edges();
				}

				result.tutorials.append(Pair<String, String>(title, link));
				continue;
			} else if (stripped_line == "@deprecated" || stripped_line.begins_with("@deprecated:")) {
				result.is_deprecated = true;
				if (stripped_line.begins_with("@deprecated:")) {
					result.deprecated_message = stripped_line.trim_prefix("@deprecated:").strip_edges();
				}
				continue;
			} else if (stripped_line == "@experimental" || stripped_line.begins_with("@experimental:")) {
				result.is_experimental = true;
				if (stripped_line.begins_with("@experimental:")) {
					result.experimental_message = stripped_line.trim_prefix("@experimental:").strip_edges();
				}
				continue;
			}
		}

		if (is_in_brief) {
			result.brief += _process_doc_line(doc_line, result.brief, space_prefix, state);
		} else {
			result.description += _process_doc_line(doc_line, result.description, space_prefix, state);
		}
	}

	return result;
}
#endif // TOOLS_ENABLED

const BSParser::ParseRule *BSParser::get_rule(BSTokenizer::Token::Type p_token_type) {
	// One parse rule together with the token type it answers for, so a row cannot silently drift
	// onto a neighbouring token. See the static assertions under the table.
	struct TokenParseRule {
		BSTokenizer::Token::Type type;
		ParseRule rule;
	};
	constexpr auto rules_are_aligned = [](const TokenParseRule *p_rules, size_t p_count) {
		for (size_t i = 0; i < p_count; i++) {
			if (p_rules[i].type != (BSTokenizer::Token::Type)i) {
				return false;
			}
		}
		return true;
	};

	// Function table for expression parsing.
	// clang-format destroys the alignment here, so turn off for the table.
	/* clang-format off */
	static constexpr TokenParseRule rules[] = {
		// TOKEN                                            PREFIX / INFIX / PRECEDENCE (for infix)
		{ BSTokenizer::Token::EMPTY, { nullptr,                                          nullptr,                                        PREC_NONE } }, // EMPTY
		// Basic
		{ BSTokenizer::Token::ANNOTATION, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ANNOTATION
		{ BSTokenizer::Token::IDENTIFIER, { &BSParser::parse_identifier,             	nullptr,                                        PREC_NONE } }, // IDENTIFIER
		{ BSTokenizer::Token::RESERVED_TYPE_NAME, { &BSParser::parse_identifier,             	nullptr,                                        PREC_NONE } }, // RESERVED_TYPE_NAME,
		{ BSTokenizer::Token::LITERAL, { &BSParser::parse_literal,                	nullptr,                                        PREC_NONE } }, // LITERAL
		// Comparison
		{ BSTokenizer::Token::LESS, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_COMPARISON } }, // LESS
		{ BSTokenizer::Token::LESS_EQUAL, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_COMPARISON } }, // LESS_EQUAL
		{ BSTokenizer::Token::GREATER, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_COMPARISON } }, // GREATER
		{ BSTokenizer::Token::GREATER_EQUAL, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_COMPARISON } }, // GREATER_EQUAL
		{ BSTokenizer::Token::EQUAL_EQUAL, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_COMPARISON } }, // EQUAL_EQUAL
		{ BSTokenizer::Token::BANG_EQUAL, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_COMPARISON } }, // BANG_EQUAL
		// Logical
		{ BSTokenizer::Token::AND, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_LOGIC_AND } }, // AND
		{ BSTokenizer::Token::OR, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_LOGIC_OR } }, // OR
		{ BSTokenizer::Token::NOT, { &BSParser::parse_unary_operator,         	&BSParser::parse_binary_not_in_operator,	PREC_CONTENT_TEST } }, // NOT
		{ BSTokenizer::Token::AMPERSAND_AMPERSAND, { nullptr,                                          &BSParser::parse_binary_operator,			PREC_LOGIC_AND } }, // AMPERSAND_AMPERSAND
		{ BSTokenizer::Token::PIPE_PIPE, { nullptr,                                          &BSParser::parse_binary_operator,			PREC_LOGIC_OR } }, // PIPE_PIPE
		{ BSTokenizer::Token::BANG, { &BSParser::parse_unary_operator,			nullptr,                                        PREC_NONE } }, // BANG
		// Bitwise
		{ BSTokenizer::Token::AMPERSAND, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_BIT_AND } }, // AMPERSAND
		{ BSTokenizer::Token::PIPE, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_BIT_OR } }, // PIPE
		{ BSTokenizer::Token::TILDE, { &BSParser::parse_unary_operator,         	nullptr,                                        PREC_NONE } }, // TILDE
		{ BSTokenizer::Token::CARET, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_BIT_XOR } }, // CARET
		{ BSTokenizer::Token::LESS_LESS, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_BIT_SHIFT } }, // LESS_LESS
		{ BSTokenizer::Token::GREATER_GREATER, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_BIT_SHIFT } }, // GREATER_GREATER
		// Math
		{ BSTokenizer::Token::PLUS, { &BSParser::parse_unary_operator,         	&BSParser::parse_binary_operator,      	PREC_ADDITION_SUBTRACTION } }, // PLUS
		{ BSTokenizer::Token::MINUS, { &BSParser::parse_unary_operator,         	&BSParser::parse_binary_operator,      	PREC_ADDITION_SUBTRACTION } }, // MINUS
		{ BSTokenizer::Token::STAR, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_FACTOR } }, // STAR
		{ BSTokenizer::Token::STAR_STAR, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_POWER } }, // STAR_STAR
		{ BSTokenizer::Token::SLASH, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_FACTOR } }, // SLASH
		{ BSTokenizer::Token::PERCENT, { &BSParser::parse_get_node,                  &BSParser::parse_binary_operator,      	PREC_FACTOR } }, // PERCENT
		// Assignment
		{ BSTokenizer::Token::EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // EQUAL
		{ BSTokenizer::Token::PLUS_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // PLUS_EQUAL
		{ BSTokenizer::Token::MINUS_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // MINUS_EQUAL
		{ BSTokenizer::Token::STAR_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // STAR_EQUAL
		{ BSTokenizer::Token::STAR_STAR_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // STAR_STAR_EQUAL
		{ BSTokenizer::Token::SLASH_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // SLASH_EQUAL
		{ BSTokenizer::Token::PERCENT_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // PERCENT_EQUAL
		{ BSTokenizer::Token::LESS_LESS_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // LESS_LESS_EQUAL
		{ BSTokenizer::Token::GREATER_GREATER_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // GREATER_GREATER_EQUAL
		{ BSTokenizer::Token::AMPERSAND_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // AMPERSAND_EQUAL
		{ BSTokenizer::Token::PIPE_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // PIPE_EQUAL
		{ BSTokenizer::Token::CARET_EQUAL, { nullptr,                                          &BSParser::parse_assignment,           	PREC_ASSIGNMENT } }, // CARET_EQUAL
		// Control flow
		{ BSTokenizer::Token::IF, { nullptr,                                          &BSParser::parse_ternary_operator,     	PREC_TERNARY } }, // IF
		{ BSTokenizer::Token::ELIF, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ELIF
		{ BSTokenizer::Token::ELSE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ELSE
		{ BSTokenizer::Token::FOR, { nullptr,                                          nullptr,                                        PREC_NONE } }, // FOR
		{ BSTokenizer::Token::WHILE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // WHILE
		{ BSTokenizer::Token::BREAK, { nullptr,                                          nullptr,                                        PREC_NONE } }, // BREAK
		{ BSTokenizer::Token::CONTINUE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // CONTINUE
		{ BSTokenizer::Token::PASS, { nullptr,                                          nullptr,                                        PREC_NONE } }, // PASS
		{ BSTokenizer::Token::RETURN, { nullptr,                                          nullptr,                                        PREC_NONE } }, // RETURN
		{ BSTokenizer::Token::MATCH, { nullptr,                                          nullptr,                                        PREC_NONE } }, // MATCH
		{ BSTokenizer::Token::WHEN, { nullptr,                                          nullptr,                                        PREC_NONE } }, // WHEN
		// Keywords
		{ BSTokenizer::Token::ABSTRACT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ABSTRACT
		{ BSTokenizer::Token::AS, { nullptr,                                          &BSParser::parse_cast,                 	PREC_CAST } }, // AS
		{ BSTokenizer::Token::ASSERT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ASSERT
		{ BSTokenizer::Token::AWAIT, { &BSParser::parse_await,                  	nullptr,                                        PREC_NONE } }, // AWAIT
		{ BSTokenizer::Token::BREAKPOINT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // BREAKPOINT
		{ BSTokenizer::Token::CLASS, { nullptr,                                          nullptr,                                        PREC_NONE } }, // CLASS
		{ BSTokenizer::Token::CLASS_NAME, { nullptr,                                          nullptr,                                        PREC_NONE } }, // CLASS_NAME
		{ BSTokenizer::Token::ENUM_NAME, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ENUM_NAME
		{ BSTokenizer::Token::TK_CONST, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TK_CONST
		{ BSTokenizer::Token::ENUM, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ENUM
		{ BSTokenizer::Token::EXTENDS, { nullptr,                                          nullptr,                                        PREC_NONE } }, // EXTENDS
		{ BSTokenizer::Token::FINAL, { nullptr,                                          nullptr,                                        PREC_NONE } }, // FINAL
		{ BSTokenizer::Token::FUNC, { &BSParser::parse_lambda,                    nullptr,                                        PREC_NONE } }, // FUNC
		{ BSTokenizer::Token::IMPORT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // IMPORT
		{ BSTokenizer::Token::TK_IN, { nullptr,                                          &BSParser::parse_binary_operator,      	PREC_CONTENT_TEST } }, // TK_IN
		{ BSTokenizer::Token::IS, { nullptr,                                          &BSParser::parse_type_test,            	PREC_TYPE_TEST } }, // IS
		{ BSTokenizer::Token::NAMESPACE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // NAMESPACE
		{ BSTokenizer::Token::PRELOAD, { &BSParser::parse_preload,					nullptr,                                        PREC_NONE } }, // PRELOAD
		{ BSTokenizer::Token::SELF, { &BSParser::parse_self,                   	nullptr,                                        PREC_NONE } }, // SELF
		{ BSTokenizer::Token::SIGNAL, { nullptr,                                          nullptr,                                        PREC_NONE } }, // SIGNAL
		{ BSTokenizer::Token::STATIC, { nullptr,                                          nullptr,                                        PREC_NONE } }, // STATIC
		{ BSTokenizer::Token::SUPER, { &BSParser::parse_call,						nullptr,                                        PREC_NONE } }, // SUPER
		{ BSTokenizer::Token::TRAIT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TRAIT
		{ BSTokenizer::Token::TRAIT_NAME, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TRAIT_NAME
		{ BSTokenizer::Token::TUPLE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TUPLE
		{ BSTokenizer::Token::TUPLE_NAME, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TUPLE_NAME
		{ BSTokenizer::Token::USES, { nullptr,                                          nullptr,                                        PREC_NONE } }, // USES
		{ BSTokenizer::Token::VAR, { nullptr,                                          nullptr,                                        PREC_NONE } }, // VAR
		{ BSTokenizer::Token::TK_VOID, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TK_VOID
		{ BSTokenizer::Token::YIELD, { &BSParser::parse_yield,                     nullptr,                                        PREC_NONE } }, // YIELD
		// Punctuation
		{ BSTokenizer::Token::BRACKET_OPEN, { &BSParser::parse_array,                  	&BSParser::parse_subscript,            	PREC_SUBSCRIPT } }, // BRACKET_OPEN
		{ BSTokenizer::Token::BRACKET_CLOSE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // BRACKET_CLOSE
		{ BSTokenizer::Token::BRACE_OPEN, { &BSParser::parse_dictionary,             	nullptr,                                        PREC_NONE } }, // BRACE_OPEN
		{ BSTokenizer::Token::BRACE_CLOSE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // BRACE_CLOSE
		{ BSTokenizer::Token::PARENTHESIS_OPEN, { &BSParser::parse_grouping,               	&BSParser::parse_call,                 	PREC_CALL } }, // PARENTHESIS_OPEN
		{ BSTokenizer::Token::PARENTHESIS_CLOSE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // PARENTHESIS_CLOSE
		{ BSTokenizer::Token::COMMA, { nullptr,                                          nullptr,                                        PREC_NONE } }, // COMMA
		{ BSTokenizer::Token::SEMICOLON, { nullptr,                                          nullptr,                                        PREC_NONE } }, // SEMICOLON
		{ BSTokenizer::Token::PERIOD, { &BSParser::parse_contextual_enum_case,	&BSParser::parse_attribute,            	PREC_ATTRIBUTE } }, // PERIOD
		{ BSTokenizer::Token::PERIOD_PERIOD, { nullptr,                                          nullptr,                                        PREC_NONE } }, // PERIOD_PERIOD
		{ BSTokenizer::Token::PERIOD_PERIOD_PERIOD, { nullptr,                                          nullptr,                                        PREC_NONE } }, // PERIOD_PERIOD_PERIOD
		{ BSTokenizer::Token::COLON, { nullptr,                                          nullptr,                                        PREC_NONE } }, // COLON
		{ BSTokenizer::Token::DOLLAR, { &BSParser::parse_get_node,               	nullptr,                                        PREC_NONE } }, // DOLLAR
		{ BSTokenizer::Token::FORWARD_ARROW, { nullptr,                                          nullptr,                                        PREC_NONE } }, // FORWARD_ARROW
		{ BSTokenizer::Token::UNDERSCORE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // UNDERSCORE
		// Whitespace
		{ BSTokenizer::Token::NEWLINE, { nullptr,                                          nullptr,                                        PREC_NONE } }, // NEWLINE
		{ BSTokenizer::Token::INDENT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // INDENT
		{ BSTokenizer::Token::DEDENT, { nullptr,                                          nullptr,                                        PREC_NONE } }, // DEDENT
		// Constants
		{ BSTokenizer::Token::CONST_PI, { &BSParser::parse_builtin_constant,			nullptr,                                        PREC_NONE } }, // CONST_PI
		{ BSTokenizer::Token::CONST_TAU, { &BSParser::parse_builtin_constant,			nullptr,                                        PREC_NONE } }, // CONST_TAU
		{ BSTokenizer::Token::CONST_INF, { &BSParser::parse_builtin_constant,			nullptr,                                        PREC_NONE } }, // CONST_INF
		{ BSTokenizer::Token::CONST_NAN, { &BSParser::parse_builtin_constant,			nullptr,                                        PREC_NONE } }, // CONST_NAN
		// Error message improvement
		{ BSTokenizer::Token::VCS_CONFLICT_MARKER, { nullptr,                                          nullptr,                                        PREC_NONE } }, // VCS_CONFLICT_MARKER
		{ BSTokenizer::Token::BACKTICK, { nullptr,                                          nullptr,                                        PREC_NONE } }, // BACKTICK
		{ BSTokenizer::Token::QUESTION_MARK, { nullptr,                                          &BSParser::parse_invalid_token,        	PREC_CAST } }, // QUESTION_MARK
		// Special
		{ BSTokenizer::Token::ERROR, { nullptr,                                          nullptr,                                        PREC_NONE } }, // ERROR
		{ BSTokenizer::Token::TK_EOF, { nullptr,                                          nullptr,                                        PREC_NONE } }, // TK_EOF
	};
	/* clang-format on */
	// Avoid desync. Upstream checks only the row count (fs_parser.cpp:7361 @ c9d5e35), which is not
	// enough: this port both adds a token type (`RESERVED_TYPE_NAME`, D1) and removes one
	// (`AS_BANG`, D1), and a table that made both edits wrongly would still have the right number of
	// rows while every rule from `IDENTIFIER` onward answered for the wrong token. Each row
	// therefore names the token it is for, and the alignment is proved at compile time.
	static_assert(std_size(rules) == BSTokenizer::Token::TK_MAX, "Amount of parse rules don't match the amount of token types.");
	static_assert(rules_are_aligned(rules, std_size(rules)), "A parse rule is not at its own token type's index.");

	// Let's assume this is never invalid, since nothing generates a TK_MAX.
	return &rules[p_token_type].rule;
}

bool BSParser::SuiteNode::has_local(const StringName &p_name) const {
	if (locals_indices.has(p_name)) {
		return true;
	}
	if (parent_block != nullptr) {
		return parent_block->has_local(p_name);
	}
	return false;
}

const BSParser::SuiteNode::Local &BSParser::SuiteNode::get_local(const StringName &p_name) const {
	if (locals_indices.has(p_name)) {
		return locals[locals_indices[p_name]];
	}
	if (parent_block != nullptr) {
		return parent_block->get_local(p_name);
	}
	return empty;
}

bool BSParser::AnnotationNode::apply(BSParser *p_this, Node *p_target, ClassNode *p_class) {
	if (is_applied) {
		return true;
	}
	is_applied = true;
	if (info == nullptr) {
		// Unresolved custom annotation usage: there is no built-in behavior to apply.
		// The analyzer resolves and validates it separately.
		return true;
	}
	return (p_this->*(p_this->valid_annotations[name].apply))(this, p_target, p_class);
}

bool BSParser::AnnotationNode::applies_to(uint32_t p_target_kinds) const {
	if (info == nullptr) {
		// Unresolved custom annotation usage. The exact declaration is resolved by the analyzer
		// later, but custom annotations only ever target the declaration surface (class, method,
		// member variable, signal, constant), so the parser attaches to those positions and keeps
		// the existing placement diagnostic for anything else (enums, statements, standalone slots,
		// script level, or a pending annotation before an `annotation` declaration).
		const uint32_t custom_targets = AnnotationInfo::CLASS | AnnotationInfo::VARIABLE | AnnotationInfo::FUNCTION | AnnotationInfo::SIGNAL | AnnotationInfo::CONSTANT | AnnotationInfo::PARAMETER;
		return (p_target_kinds & custom_targets) != 0;
	}
	return (info->target_kind & p_target_kinds) > 0;
}

bool BSParser::validate_annotation_arguments(AnnotationNode *p_annotation) {
	ERR_FAIL_COND_V_MSG(!valid_annotations.has(p_annotation->name), false, vformat(R"(Annotation "%s" not found to validate.)", p_annotation->name));

	const MethodInfo &info = valid_annotations[p_annotation->name].info;

	if (((info.flags & METHOD_FLAG_VARARG) == 0) && p_annotation->arguments.size() > info.arguments.size()) {
		push_error(vformat(R"(Annotation "%s" requires at most %d arguments, but %d were given.)", p_annotation->name, info.arguments.size(), p_annotation->arguments.size()));
		return false;
	}

	if (p_annotation->arguments.size() < info.arguments.size() - info.default_arguments.size()) {
		push_error(vformat(R"(Annotation "%s" requires at least %d arguments, but %d were given.)", p_annotation->name, info.arguments.size() - info.default_arguments.size(), p_annotation->arguments.size()));
		return false;
	}

	// Some annotations need to be resolved and applied in the parser.
	if (p_annotation->name == SNAME("@icon") || p_annotation->name == SNAME("@warning_ignore_start") || p_annotation->name == SNAME("@warning_ignore_restore")) {
		for (int i = 0; i < p_annotation->arguments.size(); i++) {
			ExpressionNode *argument = p_annotation->arguments[i];

			if (argument->type != Node::LITERAL) {
				push_error(vformat(R"(Argument %d of annotation "%s" must be a string literal.)", i + 1, p_annotation->name), argument);
				return false;
			}

			Variant value = static_cast<LiteralNode *>(argument)->value;

			if (value.get_type() != Variant::STRING) {
				push_error(vformat(R"(Argument %d of annotation "%s" must be a string literal.)", i + 1, p_annotation->name), argument);
				return false;
			}

			p_annotation->resolved_arguments.push_back(value);
		}
	}

	// For other annotations, see `BSAnalyzer::resolve_annotation()`.

	return true;
}

bool BSParser::tool_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	if (_is_tool) {
		push_error(R"("@tool" annotation can only be used once.)", p_annotation);
		return false;
	}
#endif // DEBUG_ENABLED
	_is_tool = true;
	return true;
}

bool BSParser::icon_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::CLASS, false, R"("@icon" annotation can only be applied to classes.)");
	ERR_FAIL_COND_V(p_annotation->resolved_arguments.is_empty(), false);

	ClassNode *class_node = static_cast<ClassNode *>(p_target);
	String path = p_annotation->resolved_arguments[0];

#ifdef DEBUG_ENABLED
	if (!class_node->icon_path.is_empty()) {
		push_error(R"("@icon" annotation can only be used once.)", p_annotation);
		return false;
	}
	if (path.is_empty()) {
		push_error(R"("@icon" annotation argument must contain the path to the icon.)", p_annotation->arguments[0]);
		return false;
	}
#endif // DEBUG_ENABLED

	class_node->icon_path = path;

	if (path.is_empty() || path.is_absolute_path()) {
		class_node->simplified_icon_path = path.simplify_path();
	} else if (path.is_relative_path()) {
		class_node->simplified_icon_path = script_path.get_base_dir().path_join(path).simplify_path();
	} else {
		class_node->simplified_icon_path = path;
	}

	return true;
}

bool BSParser::static_unload_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::CLASS, false, vformat(R"("%s" annotation can only be applied to classes.)", p_annotation->name));
	ClassNode *class_node = static_cast<ClassNode *>(p_target);
	if (class_node->annotated_static_unload) {
		push_error(vformat(R"("%s" annotation can only be used once per script.)", p_annotation->name), p_annotation);
		return false;
	}
	class_node->annotated_static_unload = true;
	return true;
}

bool BSParser::autoload_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(
			p_target->type != Node::CLASS,
			false,
			R"("@autoload" annotation can only be applied to the root script declaration.)");
	return true;
}

bool BSParser::noreturn_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(
			p_target->type != Node::FUNCTION,
			false,
			R"("@noreturn" annotation can only be applied to functions.)");

	FunctionNode *function_node = static_cast<FunctionNode *>(p_target);
	if (function_node->is_noreturn) {
		push_error(R"("@noreturn" annotation can only be used once per function.)", p_annotation);
		return false;
	}
	function_node->is_noreturn = true;
	return true;
}

bool BSParser::onready_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, R"("@onready" annotation can only be applied to class variables.)");

	if (current_class && !ClassDB::is_parent_class(current_class->get_datatype().native_type, SNAME("Node"))) {
		push_error(R"("@onready" can only be used in classes that inherit "Node".)", p_annotation);
		return false;
	}

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(R"("@onready" annotation cannot be applied to a static variable.)", p_annotation);
		return false;
	}
	if (variable->onready) {
		push_error(R"("@onready" annotation can only be used once per variable.)", p_annotation);
		return false;
	}
	variable->onready = true;
	current_class->onready_used = true;
	return true;
}

static String _get_annotation_error_string(const StringName &p_annotation_name, const Vector<Variant::Type> &p_expected_types, const BSParser::DataType &p_provided_type) {
	Vector<String> types;
	for (int i = 0; i < p_expected_types.size(); i++) {
		const Variant::Type &type = p_expected_types[i];
		types.push_back(Variant::get_type_name(type));
		types.push_back("Array[" + Variant::get_type_name(type) + "]");
		switch (type) {
			case Variant::INT:
				types.push_back("PackedByteArray");
				types.push_back("PackedInt32Array");
				types.push_back("PackedInt64Array");
				break;
			case Variant::FLOAT:
				types.push_back("PackedFloat32Array");
				types.push_back("PackedFloat64Array");
				break;
			case Variant::STRING:
				types.push_back("PackedStringArray");
				break;
			case Variant::VECTOR2:
				types.push_back("PackedVector2Array");
				break;
			case Variant::VECTOR3:
				types.push_back("PackedVector3Array");
				break;
			case Variant::COLOR:
				types.push_back("PackedColorArray");
				break;
			case Variant::VECTOR4:
				types.push_back("PackedVector4Array");
				break;
			default:
				break;
		}
	}

	// godot-cpp's `String` has no `quote()`; core's is `quotechar + c_escape() + quotechar`
	// (core/string/ustring.h), and `c_escape()` is mapped.
	auto quoted = [](const String &p_type_name) { return "\"" + p_type_name.c_escape() + "\""; };

	String string;
	if (types.size() == 1) {
		string = quoted(types[0]);
	} else if (types.size() == 2) {
		string = quoted(types[0]) + " or " + quoted(types[1]);
	} else if (types.size() >= 3) {
		string = quoted(types[0]);
		for (int i = 1; i < types.size() - 1; i++) {
			string += ", " + quoted(types[i]);
		}
		string += ", or " + quoted(types[types.size() - 1]);
	}

	return vformat(R"("%s" annotation requires a variable of type %s, but type "%s" was given instead.)", p_annotation_name, string, p_provided_type.to_string());
}

static StringName _find_narrowest_native_or_global_class(const BSParser::DataType &p_type) {
	switch (p_type.kind) {
		case BSParser::DataType::NATIVE: {
			if (p_type.is_meta_type) {
				// Upstream returns `Object` here because its `FSNativeClass` wrapper is not exposed
				// (fs_parser.cpp:7479 @ c9d5e35); BaristaScript has no wrapper at all, and `Object` is
				// still the narrowest exposed class a native class handle is an instance of.
				return Object::get_class_static();
			}
			return p_type.native_type;
		} break;
		case BSParser::DataType::SCRIPT: {
			Ref<Script> script;
			if (p_type.script_type.is_valid()) {
				script = p_type.script_type;
			} else {
				// godot-cpp makes ResourceLoader a singleton where core has static members
				// (recorded in src/bs_platform_manifest.json).
				script = ResourceLoader::get_singleton()->load(p_type.script_path, SNAME("Script"));
			}

			if (p_type.is_meta_type) {
				return script.is_valid() ? script->get_global_name() : Script::get_class_static();
			}
			if (script.is_null()) {
				return p_type.native_type;
			}
			if (script->get_global_name() != StringName()) {
				return script->get_global_name();
			}

			Ref<Script> base_script = script->get_base_script();
			if (base_script.is_null()) {
				return script->get_instance_base_type();
			}

			BSParser::DataType base_type;
			base_type.kind = BSParser::DataType::SCRIPT;
			base_type.builtin_type = Variant::OBJECT;
			base_type.native_type = base_script->get_instance_base_type();
			base_type.script_type = base_script;
			base_type.script_path = base_script->get_path();

			return _find_narrowest_native_or_global_class(base_type);
		} break;
		case BSParser::DataType::CLASS: {
			if (p_type.is_meta_type) {
				return BaristaScript::get_class_static();
			}
			if (p_type.class_type == nullptr) {
				return p_type.native_type;
			}
			if (p_type.class_type->get_global_name() != StringName()) {
				return p_type.class_type->get_global_name();
			}
			return _find_narrowest_native_or_global_class(p_type.class_type->base_type);
		} break;
		default: {
			ERR_FAIL_V(StringName());
		} break;
	}
}

template <PropertyHint t_hint, Variant::Type t_type>
bool BSParser::export_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));
	ERR_FAIL_NULL_V(p_class, false);

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	variable->exported = true;

	variable->export_info.type = t_type;
	variable->export_info.hint = t_hint;

	String hint_string;
	for (int i = 0; i < p_annotation->resolved_arguments.size(); i++) {
		String arg_string = String(p_annotation->resolved_arguments[i]);

		if (p_annotation->name != SNAME("@export_placeholder")) {
			if (arg_string.is_empty()) {
				push_error(vformat(R"(Argument %d of annotation "%s" is empty.)", i + 1, p_annotation->name), p_annotation->arguments[i]);
				return false;
			}
			// godot-cpp's `String` has no `contains_char`; `contains` is the same test.
			if (arg_string.contains(",")) {
				push_error(vformat(R"(Argument %d of annotation "%s" contains a comma. Use separate arguments instead.)", i + 1, p_annotation->name), p_annotation->arguments[i]);
				return false;
			}
		}

		// WARNING: Do not merge with the previous `if` because there `!=`, not `==`!
		if (p_annotation->name == SNAME("@export_flags")) {
			const int64_t max_flags = 32;
			const PackedStringArray t = arg_string.split(":", true, 1);
			if (t[0].is_empty()) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": Expected flag name.)", i + 1), p_annotation->arguments[i]);
				return false;
			}
			if (t.size() == 2) {
				if (t[1].is_empty()) {
					push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": Expected flag value.)", i + 1), p_annotation->arguments[i]);
					return false;
				}
				if (!t[1].is_valid_int()) {
					push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": The flag value must be a valid integer.)", i + 1), p_annotation->arguments[i]);
					return false;
				}
				int64_t value = t[1].to_int();
				if (value < 1 || value >= (1LL << max_flags)) {
					push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": The flag value must be at least 1 and at most 2 ** %d - 1.)", i + 1, max_flags), p_annotation->arguments[i]);
					return false;
				}
			} else if (i >= max_flags) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_flags": Starting from argument %d, the flag value must be specified explicitly.)", i + 1, max_flags + 1), p_annotation->arguments[i]);
				return false;
			}
		} else if (p_annotation->name == SNAME("@export_node_path")) {
			String native_class = arg_string;
			// Upstream asks the engine-internal `ScriptServer` (fs_parser.cpp:7566-7567 @ c9d5e35).
			// The same table reaches a GDExtension as `ProjectSettings::get_global_class_list()`.
			const String global_class_base = _global_class_native_base(arg_string);
			if (!global_class_base.is_empty()) {
				native_class = global_class_base;
			}
			// `ClassDB::is_class_exposed()` has no godot-cpp counterpart and needs none: the ClassDB an
			// extension talks to is built from the exposed API, so `class_exists()` already answers it.
			if (!ClassDB::class_exists(native_class)) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_node_path": The class "%s" was not found in the global scope.)", i + 1, arg_string), p_annotation->arguments[i]);
				return false;
			} else if (!ClassDB::is_parent_class(native_class, SNAME("Node"))) {
				push_error(vformat(R"(Invalid argument %d of annotation "@export_node_path": The class "%s" does not inherit "Node".)", i + 1, arg_string), p_annotation->arguments[i]);
				return false;
			}
		}

		if (i > 0) {
			hint_string += ",";
		}
		hint_string += arg_string;
	}
	variable->export_info.hint_string = hint_string;

	// This is called after the analyzer is done finding the type, so this should be set here.
	DataType export_type = variable->get_datatype();

	// Use initializer type if specified type is `Variant`.
	if (export_type.is_variant() && variable->initializer != nullptr && variable->initializer->datatype.is_set()) {
		export_type = variable->initializer->get_datatype();
		export_type.type_source = DataType::INFERRED;
	}

	const Variant::Type original_export_type_builtin = export_type.builtin_type;

	// Process array and packed array annotations on the element type.
	bool is_array = false;
	if (export_type.builtin_type == Variant::ARRAY && export_type.has_container_element_type(0)) {
		is_array = true;
		export_type = export_type.get_container_element_type(0);
	} else if (export_type.is_typed_container_type()) {
		is_array = true;
		export_type = export_type.get_typed_container_type();
		export_type.type_source = variable->datatype.type_source;
	}

	bool is_dict = false;
	if (export_type.builtin_type == Variant::DICTIONARY && export_type.has_container_element_types()) {
		is_dict = true;
		DataType inner_type = export_type.get_container_element_type_or_variant(1);
		export_type = export_type.get_container_element_type_or_variant(0);
		export_type.set_container_element_type(0, inner_type); // Store earlier extracted value within key to separately parse after.
	}

	bool use_default_variable_type_check = true;

	if (p_annotation->name == SNAME("@export_range")) {
		if (export_type.builtin_type == Variant::INT) {
			variable->export_info.type = Variant::INT;
		}
	} else if (p_annotation->name == SNAME("@export_multiline")) {
		use_default_variable_type_check = false;

		if (export_type.builtin_type != Variant::STRING && export_type.builtin_type != Variant::DICTIONARY) {
			Vector<Variant::Type> expected_types = { Variant::STRING, Variant::DICTIONARY };
			push_error(_get_annotation_error_string(p_annotation->name, expected_types, variable->get_datatype()), p_annotation);
			return false;
		}

		if (export_type.builtin_type == Variant::DICTIONARY) {
			variable->export_info.type = Variant::DICTIONARY;
		}
	} else if (p_annotation->name == SNAME("@export")) {
		use_default_variable_type_check = false;

		if (variable->datatype_specifier == nullptr && variable->initializer == nullptr) {
			push_error(R"(Cannot use simple "@export" annotation with variable without type or initializer, since type can't be inferred.)", p_annotation);
			return false;
		}

		if (export_type.has_no_type()) {
			push_error(R"(Cannot use simple "@export" annotation because the type of the initialized value can't be inferred.)", p_annotation);
			return false;
		}

		switch (export_type.kind) {
			case BSParser::DataType::BUILTIN:
				variable->export_info.type = export_type.builtin_type;
				variable->export_info.hint = PROPERTY_HINT_NONE;
				variable->export_info.hint_string = String();
				break;
			case BSParser::DataType::NATIVE:
			case BSParser::DataType::SCRIPT:
			case BSParser::DataType::CLASS: {
				const StringName class_name = _find_narrowest_native_or_global_class(export_type);
				if (ClassDB::is_parent_class(export_type.native_type, SNAME("Resource"))) {
					variable->export_info.type = Variant::OBJECT;
					variable->export_info.hint = PROPERTY_HINT_RESOURCE_TYPE;
					variable->export_info.hint_string = class_name;
				} else if (ClassDB::is_parent_class(export_type.native_type, SNAME("Node"))) {
					variable->export_info.type = Variant::OBJECT;
					variable->export_info.hint = PROPERTY_HINT_NODE_TYPE;
					variable->export_info.hint_string = class_name;
				} else {
					push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
					return false;
				}
			} break;
			case BSParser::DataType::ENUM: {
				if (export_type.is_meta_type) {
					variable->export_info.type = Variant::DICTIONARY;
				} else {
					variable->export_info.type = Variant::INT;
					variable->export_info.hint = PROPERTY_HINT_ENUM;

					String enum_hint_string;
					bool first = true;
					for (const KeyValue<StringName, int64_t> &E : export_type.enum_values) {
						if (!first) {
							enum_hint_string += ",";
						} else {
							first = false;
						}
						enum_hint_string += String(E.key).capitalize().xml_escape();
						enum_hint_string += ":";
						enum_hint_string += String::num_int64(E.value).xml_escape();
					}

					variable->export_info.hint_string = enum_hint_string;
					variable->export_info.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
					variable->export_info.class_name = String(export_type.native_type).replace("::", ".");
				}
			} break;
			case BSParser::DataType::VARIANT: {
				if (export_type.is_variant()) {
					variable->export_info.type = Variant::NIL;
					variable->export_info.usage |= PROPERTY_USAGE_NIL_IS_VARIANT;
				}
			} break;
			case BSParser::DataType::UNION:
				// A `PropertyInfo` carries exactly one runtime type, so a multi-member union cannot be
				// exported. A single-member alias never reaches here, having collapsed to its member
				// (with that member's runtime typing) during normalization.
				push_error(vformat(R"(Cannot export type union "%s": a multi-member union has no single runtime type to export.)", export_type.to_string()), p_annotation);
				return false;
			default:
				push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
				return false;
		}

		if (variable->export_info.hint == PROPERTY_HINT_NODE_TYPE && !ClassDB::is_parent_class(p_class->base_type.native_type, SNAME("Node"))) {
			push_error(vformat(R"(Node export is only supported in Node-derived classes, but the current class inherits "%s".)", p_class->base_type.to_string()), p_annotation);
			return false;
		}

		if (is_dict) {
			String key_prefix = itos(variable->export_info.type);
			if (variable->export_info.hint) {
				key_prefix += "/" + itos(variable->export_info.hint);
			}
			key_prefix += ":" + variable->export_info.hint_string;

			// Now parse value.
			export_type = export_type.get_container_element_type(0);

			if (export_type.is_variant() || export_type.has_no_type()) {
				export_type.kind = BSParser::DataType::BUILTIN;
			}
			switch (export_type.kind) {
				case BSParser::DataType::BUILTIN:
					variable->export_info.type = export_type.builtin_type;
					variable->export_info.hint = PROPERTY_HINT_NONE;
					variable->export_info.hint_string = String();
					break;
				case BSParser::DataType::NATIVE:
				case BSParser::DataType::SCRIPT:
				case BSParser::DataType::CLASS: {
					const StringName class_name = _find_narrowest_native_or_global_class(export_type);
					if (ClassDB::is_parent_class(export_type.native_type, SNAME("Resource"))) {
						variable->export_info.type = Variant::OBJECT;
						variable->export_info.hint = PROPERTY_HINT_RESOURCE_TYPE;
						variable->export_info.hint_string = class_name;
					} else if (ClassDB::is_parent_class(export_type.native_type, SNAME("Node"))) {
						variable->export_info.type = Variant::OBJECT;
						variable->export_info.hint = PROPERTY_HINT_NODE_TYPE;
						variable->export_info.hint_string = class_name;
					} else {
						push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
						return false;
					}
				} break;
				case BSParser::DataType::ENUM: {
					if (export_type.is_meta_type) {
						variable->export_info.type = Variant::DICTIONARY;
					} else {
						variable->export_info.type = Variant::INT;
						variable->export_info.hint = PROPERTY_HINT_ENUM;

						String enum_hint_string;
						bool first = true;
						for (const KeyValue<StringName, int64_t> &E : export_type.enum_values) {
							if (!first) {
								enum_hint_string += ",";
							} else {
								first = false;
							}
							enum_hint_string += String(E.key).capitalize().xml_escape();
							enum_hint_string += ":";
							enum_hint_string += String::num_int64(E.value).xml_escape();
						}

						variable->export_info.hint_string = enum_hint_string;
						variable->export_info.usage |= PROPERTY_USAGE_CLASS_IS_ENUM;
						variable->export_info.class_name = String(export_type.native_type).replace("::", ".");
					}
				} break;
				default:
					push_error(R"(Export type can only be built-in, a resource, a node, or an enum.)", p_annotation);
					return false;
			}

			if (variable->export_info.hint == PROPERTY_HINT_NODE_TYPE && !ClassDB::is_parent_class(p_class->base_type.native_type, SNAME("Node"))) {
				push_error(vformat(R"(Node export is only supported in Node-derived classes, but the current class inherits "%s".)", p_class->base_type.to_string()), p_annotation);
				return false;
			}

			String value_prefix = itos(variable->export_info.type);
			if (variable->export_info.hint) {
				value_prefix += "/" + itos(variable->export_info.hint);
			}
			value_prefix += ":" + variable->export_info.hint_string;

			variable->export_info.type = Variant::DICTIONARY;
			variable->export_info.hint = PROPERTY_HINT_TYPE_STRING;
			variable->export_info.hint_string = key_prefix + ";" + value_prefix;
			variable->export_info.usage = PROPERTY_USAGE_DEFAULT;
			variable->export_info.class_name = StringName();
		}
	} else if (p_annotation->name == SNAME("@export_enum")) {
		use_default_variable_type_check = false;

		Variant::Type enum_type = Variant::INT;

		if (export_type.kind == DataType::BUILTIN && export_type.builtin_type == Variant::STRING) {
			enum_type = Variant::STRING;
		}

		variable->export_info.type = enum_type;

		if (!export_type.is_variant() && (export_type.kind != DataType::BUILTIN || export_type.builtin_type != enum_type)) {
			Vector<Variant::Type> expected_types = { Variant::INT, Variant::STRING };
			push_error(_get_annotation_error_string(p_annotation->name, expected_types, variable->get_datatype()), p_annotation);
			return false;
		}
	}

	if (use_default_variable_type_check) {
		// Validate variable type with export.
		if (!export_type.is_variant() && (export_type.kind != DataType::BUILTIN || export_type.builtin_type != t_type)) {
			// Allow float/int conversion.
			if ((t_type != Variant::FLOAT || export_type.builtin_type != Variant::INT) && (t_type != Variant::INT || export_type.builtin_type != Variant::FLOAT)) {
				Vector<Variant::Type> expected_types = { t_type };
				push_error(_get_annotation_error_string(p_annotation->name, expected_types, variable->get_datatype()), p_annotation);
				return false;
			}
		}
	}

	if (is_array) {
		String hint_prefix = itos(variable->export_info.type);
		if (variable->export_info.hint) {
			hint_prefix += "/" + itos(variable->export_info.hint);
		}
		variable->export_info.type = original_export_type_builtin;
		variable->export_info.hint = PROPERTY_HINT_TYPE_STRING;
		variable->export_info.hint_string = hint_prefix + ":" + variable->export_info.hint_string;
		variable->export_info.usage = PROPERTY_USAGE_DEFAULT;
		variable->export_info.class_name = StringName();
	}

	return true;
}

// For `@export_storage` and `@export_custom`, there is no need to check the variable type, argument values,
// or handle array exports in a special way, so they are implemented as separate methods.

bool BSParser::export_storage_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	variable->exported = true;

	// Save the info because the compiler uses export info for overwriting member info.
	variable->export_info = variable->get_datatype().to_property_info(variable->identifier->name);
	variable->export_info.usage |= PROPERTY_USAGE_STORAGE;

	return true;
}

bool BSParser::export_custom_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));
	ERR_FAIL_COND_V_MSG(p_annotation->resolved_arguments.size() < 2, false, R"(Annotation "@export_custom" requires 2 arguments.)");

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	variable->exported = true;

	DataType export_type = variable->get_datatype();

	variable->export_info.type = export_type.builtin_type;
	variable->export_info.hint = static_cast<PropertyHint>(p_annotation->resolved_arguments[0].operator int64_t());
	variable->export_info.hint_string = p_annotation->resolved_arguments[1];

	if (p_annotation->resolved_arguments.size() >= 3) {
		variable->export_info.usage = p_annotation->resolved_arguments[2].operator int64_t();
	}
	return true;
}

bool BSParser::export_tool_button_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef TOOLS_ENABLED
	ERR_FAIL_COND_V_MSG(p_target->type != Node::VARIABLE, false, vformat(R"("%s" annotation can only be applied to variables.)", p_annotation->name));
	ERR_FAIL_COND_V(p_annotation->resolved_arguments.is_empty(), false);

	if (!is_tool()) {
		push_error(R"(Tool buttons can only be used in tool scripts (add "@tool" to the top of the script).)", p_annotation);
		return false;
	}

	VariableNode *variable = static_cast<VariableNode *>(p_target);

	if (variable->is_static) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to a static variable.)", p_annotation->name), p_annotation);
		return false;
	}
	if (variable->exported) {
		push_error(vformat(R"(Annotation "%s" cannot be used with another "@export" annotation.)", p_annotation->name), p_annotation);
		return false;
	}

	const DataType variable_type = variable->get_datatype();
	if (!variable_type.is_variant() && variable_type.is_hard_type()) {
		if (variable_type.kind != DataType::BUILTIN || variable_type.builtin_type != Variant::CALLABLE) {
			push_error(vformat(R"("@export_tool_button" annotation requires a variable of type "Callable", but type "%s" was given instead.)", variable_type.to_string()), p_annotation);
			return false;
		}
	}

	variable->exported = true;

	// Build the hint string (format: `<text>[,<icon>]`).
	String hint_string = p_annotation->resolved_arguments[0].operator String(); // Button text.
	if (p_annotation->resolved_arguments.size() > 1) {
		hint_string += "," + p_annotation->resolved_arguments[1].operator String(); // Button icon.
	}

	variable->export_info.type = Variant::CALLABLE;
	variable->export_info.hint = PROPERTY_HINT_TOOL_BUTTON;
	variable->export_info.hint_string = hint_string;
	variable->export_info.usage = PROPERTY_USAGE_EDITOR;
#endif // TOOLS_ENABLED

	return true; // Only available in editor.
}

bool BSParser::keep_name_annotation(AnnotationNode *, Node *, ClassNode *) {
	return true;
}

template <PropertyUsageFlags t_usage>
bool BSParser::export_group_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V(p_annotation->resolved_arguments.is_empty(), false);

	p_annotation->export_info.name = p_annotation->resolved_arguments[0];

	switch (t_usage) {
		case PROPERTY_USAGE_CATEGORY: {
			p_annotation->export_info.usage = t_usage;
		} break;

		case PROPERTY_USAGE_GROUP: {
			p_annotation->export_info.usage = t_usage;
			if (p_annotation->resolved_arguments.size() == 2) {
				p_annotation->export_info.hint_string = p_annotation->resolved_arguments[1];
			}
		} break;

		case PROPERTY_USAGE_SUBGROUP: {
			p_annotation->export_info.usage = t_usage;
			if (p_annotation->resolved_arguments.size() == 2) {
				p_annotation->export_info.hint_string = p_annotation->resolved_arguments[1];
			}
		} break;
	}

	return true;
}

bool BSParser::warning_ignore_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	bool has_error = false;
	for (const Variant &warning_name : p_annotation->resolved_arguments) {
		BSWarning::Code warning_code = BSWarning::get_code_from_name(String(warning_name).to_upper());
		if (warning_code == BSWarning::WARNING_MAX) {
			push_error(vformat(R"(Invalid warning name: "%s".)", warning_name), p_annotation);
			has_error = true;
		} else {
			int start_line = p_annotation->start_line;
			int end_line = p_target->end_line;

			switch (p_target->type) {
#define SIMPLE_CASE(m_type, m_class, m_property)          \
	case m_type: {                                        \
		m_class *node = static_cast<m_class *>(p_target); \
		if (node->m_property == nullptr) {                \
			end_line = node->start_line;                  \
		} else {                                          \
			end_line = node->m_property->end_line;        \
		}                                                 \
	} break;

				// Can contain properties (set/get).
				SIMPLE_CASE(Node::VARIABLE, VariableNode, initializer)
#undef SIMPLE_CASE

				case Node::CLASS: {
					end_line = p_target->start_line;
					for (const AnnotationNode *annotation : p_target->annotations) {
						start_line = MIN(start_line, annotation->start_line);
						end_line = MAX(end_line, annotation->end_line);
					}
				} break;

				case Node::FUNCTION: {
					FunctionNode *function = static_cast<FunctionNode *>(p_target);
					end_line = function->start_line;
					for (int i = 0; i < function->parameters.size(); i++) {
						end_line = MAX(end_line, function->parameters[i]->end_line);
						if (function->parameters[i]->initializer != nullptr) {
							end_line = MAX(end_line, function->parameters[i]->initializer->end_line);
						}
					}
				} break;

				case Node::MATCH_BRANCH: {
					MatchBranchNode *branch = static_cast<MatchBranchNode *>(p_target);
					end_line = branch->start_line;
					for (int i = 0; i < branch->patterns.size(); i++) {
						end_line = MAX(end_line, branch->patterns[i]->end_line);
					}
				} break;

				default: {
				} break;
			}

			end_line = MAX(start_line, end_line); // Prevent infinite loop.
			for (int line = start_line; line <= end_line; line++) {
				warning_ignored_lines[warning_code].insert(line);
			}
		}
	}
	return !has_error;
#else // !DEBUG_ENABLED
	// Only available in debug builds.
	return true;
#endif // DEBUG_ENABLED
}

bool BSParser::warning_ignore_region_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	bool has_error = false;
	const bool is_start = p_annotation->name == SNAME("@warning_ignore_start");
	for (const Variant &warning_name : p_annotation->resolved_arguments) {
		BSWarning::Code warning_code = BSWarning::get_code_from_name(String(warning_name).to_upper());
		if (warning_code == BSWarning::WARNING_MAX) {
			push_error(vformat(R"(Invalid warning name: "%s".)", warning_name), p_annotation);
			has_error = true;
			continue;
		}
		if (is_start) {
			if (warning_ignore_start_lines[warning_code] != INT_MAX) {
				push_error(vformat(R"(Warning "%s" is already being ignored by "@warning_ignore_start" at line %d.)", String(warning_name).to_upper(), warning_ignore_start_lines[warning_code]), p_annotation);
				has_error = true;
				continue;
			}
			warning_ignore_start_lines[warning_code] = p_annotation->start_line;
		} else {
			if (warning_ignore_start_lines[warning_code] == INT_MAX) {
				push_error(vformat(R"(Warning "%s" is not being ignored by "@warning_ignore_start".)", String(warning_name).to_upper()), p_annotation);
				has_error = true;
				continue;
			}
			const int start_line = warning_ignore_start_lines[warning_code];
			const int end_line = MAX(start_line, p_annotation->start_line); // Prevent infinite loop.
			for (int i = start_line; i <= end_line; i++) {
				warning_ignored_lines[warning_code].insert(i);
			}
			warning_ignore_start_lines[warning_code] = INT_MAX;
		}
	}
	return !has_error;
#else // !DEBUG_ENABLED
	// Only available in debug builds.
	return true;
#endif // DEBUG_ENABLED
}

bool BSParser::rpc_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	ERR_FAIL_COND_V_MSG(p_target->type != Node::FUNCTION, false, vformat(R"("%s" annotation can only be applied to functions.)", p_annotation->name));

	FunctionNode *function = static_cast<FunctionNode *>(p_target);
	if (function->rpc_config.get_type() != Variant::NIL) {
		push_error(R"(RPC annotations can only be used once per function.)", p_annotation);
		return false;
	}

	// Default values should match the annotation registration defaults and `SceneRPCInterface::_parse_rpc_config()`.
	Dictionary rpc_config;
	rpc_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
	if (!p_annotation->resolved_arguments.is_empty()) {
		unsigned char locality_args = 0;
		unsigned char permission_args = 0;
		unsigned char transfer_mode_args = 0;

		for (int i = 0; i < p_annotation->resolved_arguments.size(); i++) {
			if (i == 3) {
				rpc_config["channel"] = p_annotation->resolved_arguments[i].operator int();
				continue;
			}

			String arg = p_annotation->resolved_arguments[i].operator String();
			if (arg == "call_local") {
				locality_args++;
				rpc_config["call_local"] = true;
			} else if (arg == "call_remote") {
				locality_args++;
				rpc_config["call_local"] = false;
			} else if (arg == "any_peer") {
				permission_args++;
				rpc_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_ANY_PEER;
			} else if (arg == "authority") {
				permission_args++;
				rpc_config["rpc_mode"] = MultiplayerAPI::RPC_MODE_AUTHORITY;
			} else if (arg == "reliable") {
				transfer_mode_args++;
				rpc_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_RELIABLE;
			} else if (arg == "unreliable") {
				transfer_mode_args++;
				rpc_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
			} else if (arg == "unreliable_ordered") {
				transfer_mode_args++;
				rpc_config["transfer_mode"] = MultiplayerPeer::TRANSFER_MODE_UNRELIABLE_ORDERED;
			} else {
				push_error(R"(Invalid RPC argument. Must be one of: "call_local"/"call_remote" (local calls), "any_peer"/"authority" (permission), "reliable"/"unreliable"/"unreliable_ordered" (transfer mode).)", p_annotation);
			}
		}

		if (locality_args > 1) {
			push_error(R"(Invalid RPC config. The locality ("call_local"/"call_remote") must be specified no more than once.)", p_annotation);
		} else if (permission_args > 1) {
			push_error(R"(Invalid RPC config. The permission ("any_peer"/"authority") must be specified no more than once.)", p_annotation);
		} else if (transfer_mode_args > 1) {
			push_error(R"(Invalid RPC config. The transfer mode ("reliable"/"unreliable"/"unreliable_ordered") must be specified no more than once.)", p_annotation);
		}
	}
	function->rpc_config = rpc_config;
	return true;
}

BSParser::DataType BSParser::SuiteNode::Local::get_datatype() const {
	switch (type) {
		case CONSTANT:
			return constant->get_datatype();
		case VARIABLE:
			return variable->get_datatype();
		case PARAMETER:
			return parameter->get_datatype();
		case FOR_VARIABLE:
		case PATTERN_BIND:
		case CASE_BIND:
			return bind->get_datatype();
		case UNDEFINED:
			return DataType();
	}
	return DataType();
}

String BSParser::SuiteNode::Local::get_name() const {
	switch (type) {
		case SuiteNode::Local::PARAMETER:
			return "parameter";
		case SuiteNode::Local::CONSTANT:
			return "constant";
		case SuiteNode::Local::VARIABLE:
			return "variable";
		case SuiteNode::Local::FOR_VARIABLE:
			return "for loop iterator";
		case SuiteNode::Local::PATTERN_BIND:
			return "pattern bind";
		case SuiteNode::Local::CASE_BIND:
			return "case bind";
		case SuiteNode::Local::UNDEFINED:
			return "<undefined>";
		default:
			return String();
	}
}

void BSParser::complete_extents(Node *p_node) {
	while (!nodes_in_progress.is_empty() && nodes_in_progress.back()->get() != p_node) {
		ERR_PRINT("Parser bug: Mismatch in extents tracking stack.");
		nodes_in_progress.pop_back();
	}
	if (nodes_in_progress.is_empty()) {
		ERR_PRINT("Parser bug: Extents tracking stack is empty.");
	} else {
		nodes_in_progress.pop_back();
	}
}

void BSParser::update_extents(Node *p_node) {
	p_node->end_line = previous.end_line;
	p_node->end_column = previous.end_column;
}

void BSParser::reset_extents(Node *p_node, BSTokenizer::Token p_token) {
	p_node->start_line = p_token.start_line;
	p_node->end_line = p_token.end_line;
	p_node->start_column = p_token.start_column;
	p_node->end_column = p_token.end_column;
}

void BSParser::reset_extents(Node *p_node, Node *p_from) {
	if (p_from == nullptr) {
		return;
	}
	p_node->start_line = p_from->start_line;
	p_node->end_line = p_from->end_line;
	p_node->start_column = p_from->start_column;
	p_node->end_column = p_from->end_column;
}

/*---------- PRETTY PRINT FOR DEBUG ----------*/

#ifdef DEBUG_ENABLED

void BSParser::TreePrinter::increase_indent() {
	indent_level++;
	indent = "";
	for (int i = 0; i < indent_level * 4; i++) {
		if (i % 4 == 0) {
			indent += "|";
		} else {
			indent += " ";
		}
	}
}

void BSParser::TreePrinter::decrease_indent() {
	indent_level--;
	indent = "";
	for (int i = 0; i < indent_level * 4; i++) {
		if (i % 4 == 0) {
			indent += "|";
		} else {
			indent += " ";
		}
	}
}

void BSParser::TreePrinter::push_line(const String &p_line) {
	if (!p_line.is_empty()) {
		push_text(p_line);
	}
	printed += "\n";
	pending_indent = true;
}

void BSParser::TreePrinter::push_text(const String &p_text) {
	if (pending_indent) {
		printed += indent;
		pending_indent = false;
	}
	printed += p_text;
}

void BSParser::TreePrinter::print_annotation(const AnnotationNode *p_annotation) {
	if (p_annotation == nullptr) {
		push_text("<invalid annotation>");
		return;
	}
	push_text(p_annotation->name);
	push_text(" (");
	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_annotation->arguments[i]);
	}
	push_line(")");
}

void BSParser::TreePrinter::print_annotation_declaration(AnnotationDeclarationNode *p_annotation_declaration) {
	if (p_annotation_declaration == nullptr) {
		push_text("<invalid annotation declaration>");
		return;
	}
	push_text("Annotation ");
	if (p_annotation_declaration->identifier == nullptr) {
		push_text("<unnamed>");
	} else {
		print_identifier(p_annotation_declaration->identifier);
	}

	push_text("(");
	for (int i = 0; i < p_annotation_declaration->parameters.size(); i++) {
		if (i > 0) {
			push_text(", ");
		}
		print_parameter(p_annotation_declaration->parameters[i]);
	}
	if (p_annotation_declaration->rest_parameter != nullptr) {
		if (!p_annotation_declaration->parameters.is_empty()) {
			push_text(", ");
		}
		push_text("...");
		print_parameter(p_annotation_declaration->rest_parameter);
	}
	push_text(")");

	push_text(" targets ");
	bool first = true;
	const uint32_t targets = p_annotation_declaration->targets;
	if (targets & AnnotationDeclarationNode::TARGET_CLASS) {
		push_text("CLASS");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_METHOD) {
		push_text(first ? "METHOD" : ", METHOD");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_VARIABLE) {
		push_text(first ? "VARIABLE" : ", VARIABLE");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_SIGNAL) {
		push_text(first ? "SIGNAL" : ", SIGNAL");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_CONSTANT) {
		push_text(first ? "CONSTANT" : ", CONSTANT");
		first = false;
	}
	if (targets & AnnotationDeclarationNode::TARGET_PARAMETER) {
		push_text(first ? "PARAMETER" : ", PARAMETER");
		first = false;
	}

	push_line(vformat(" [%s]", p_annotation_declaration->qualified_name));
}

void BSParser::TreePrinter::print_array(ArrayNode *p_array) {
	if (p_array == nullptr) {
		push_text("<invalid array>");
		return;
	}
	push_text("[ ");
	for (int i = 0; i < p_array->elements.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_array->elements[i]);
	}
	push_text(" ]");
}

void BSParser::TreePrinter::print_assert(AssertNode *p_assert) {
	if (p_assert == nullptr) {
		push_text("<invalid assert>");
		return;
	}
	push_text("Assert ( ");
	print_expression(p_assert->condition);
	push_line(" )");
}

void BSParser::TreePrinter::print_assignment(AssignmentNode *p_assignment) {
	if (p_assignment == nullptr) {
		push_text("<invalid assignment>");
		return;
	}
	switch (p_assignment->assignee->type) {
		case Node::IDENTIFIER:
			print_identifier(static_cast<IdentifierNode *>(p_assignment->assignee));
			break;
		case Node::SUBSCRIPT:
			print_subscript(static_cast<SubscriptNode *>(p_assignment->assignee));
			break;
		default:
			break; // Unreachable.
	}

	push_text(" ");
	switch (p_assignment->operation) {
		case AssignmentNode::OP_ADDITION:
			push_text("+");
			break;
		case AssignmentNode::OP_SUBTRACTION:
			push_text("-");
			break;
		case AssignmentNode::OP_MULTIPLICATION:
			push_text("*");
			break;
		case AssignmentNode::OP_DIVISION:
			push_text("/");
			break;
		case AssignmentNode::OP_MODULO:
			push_text("%");
			break;
		case AssignmentNode::OP_POWER:
			push_text("**");
			break;
		case AssignmentNode::OP_BIT_SHIFT_LEFT:
			push_text("<<");
			break;
		case AssignmentNode::OP_BIT_SHIFT_RIGHT:
			push_text(">>");
			break;
		case AssignmentNode::OP_BIT_AND:
			push_text("&");
			break;
		case AssignmentNode::OP_BIT_OR:
			push_text("|");
			break;
		case AssignmentNode::OP_BIT_XOR:
			push_text("^");
			break;
		case AssignmentNode::OP_NONE:
			break;
	}
	push_text("= ");
	print_expression(p_assignment->assigned_value);
	push_line();
}

void BSParser::TreePrinter::print_await(AwaitNode *p_await) {
	if (p_await == nullptr) {
		push_text("<invalid await>");
		return;
	}
	push_text("Await ");
	print_expression(p_await->to_await);
}

void BSParser::TreePrinter::print_binary_op(BinaryOpNode *p_binary_op) {
	if (p_binary_op == nullptr) {
		push_text("<invalid binary op>");
		return;
	}
	// Surround in parenthesis for disambiguation.
	push_text("(");
	print_expression(p_binary_op->left_operand);
	switch (p_binary_op->operation) {
		case BinaryOpNode::OP_ADDITION:
			push_text(" + ");
			break;
		case BinaryOpNode::OP_SUBTRACTION:
			push_text(" - ");
			break;
		case BinaryOpNode::OP_MULTIPLICATION:
			push_text(" * ");
			break;
		case BinaryOpNode::OP_DIVISION:
			push_text(" / ");
			break;
		case BinaryOpNode::OP_MODULO:
			push_text(" % ");
			break;
		case BinaryOpNode::OP_POWER:
			push_text(" ** ");
			break;
		case BinaryOpNode::OP_BIT_LEFT_SHIFT:
			push_text(" << ");
			break;
		case BinaryOpNode::OP_BIT_RIGHT_SHIFT:
			push_text(" >> ");
			break;
		case BinaryOpNode::OP_BIT_AND:
			push_text(" & ");
			break;
		case BinaryOpNode::OP_BIT_OR:
			push_text(" | ");
			break;
		case BinaryOpNode::OP_BIT_XOR:
			push_text(" ^ ");
			break;
		case BinaryOpNode::OP_LOGIC_AND:
			push_text(" AND ");
			break;
		case BinaryOpNode::OP_LOGIC_OR:
			push_text(" OR ");
			break;
		case BinaryOpNode::OP_CONTENT_TEST:
			push_text(" IN ");
			break;
		case BinaryOpNode::OP_COMP_EQUAL:
			push_text(" == ");
			break;
		case BinaryOpNode::OP_COMP_NOT_EQUAL:
			push_text(" != ");
			break;
		case BinaryOpNode::OP_COMP_LESS:
			push_text(" < ");
			break;
		case BinaryOpNode::OP_COMP_LESS_EQUAL:
			push_text(" <= ");
			break;
		case BinaryOpNode::OP_COMP_GREATER:
			push_text(" > ");
			break;
		case BinaryOpNode::OP_COMP_GREATER_EQUAL:
			push_text(" >= ");
			break;
	}
	print_expression(p_binary_op->right_operand);
	// Surround in parenthesis for disambiguation.
	push_text(")");
}

void BSParser::TreePrinter::print_call(CallNode *p_call) {
	if (p_call == nullptr) {
		push_text("<invalid call>");
		return;
	}
	if (p_call->is_super) {
		push_text("super");
		if (p_call->callee != nullptr) {
			push_text(".");
			print_expression(p_call->callee);
		}
	} else {
		print_expression(p_call->callee);
	}
	push_text("( ");
	for (int i = 0; i < p_call->arguments.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_call->arguments[i]);
	}
	push_text(" )");
}

void BSParser::TreePrinter::print_cast(CastNode *p_cast) {
	if (p_cast == nullptr) {
		push_text("<invalid cast>");
		return;
	}
	print_expression(p_cast->operand);
	push_text(" AS ");
	print_type(p_cast->cast_type);
}

void BSParser::TreePrinter::print_class(ClassNode *p_class) {
	if (p_class == nullptr) {
		push_text("<invalid class>");
		return;
	}
	for (const AnnotationNode *E : p_class->annotations) {
		print_annotation(E);
	}
	push_text(p_class->is_trait ? "Trait " : "Class ");
	if (p_class->identifier == nullptr) {
		push_text("<unnamed>");
	} else {
		print_identifier(p_class->identifier);
	}

	print_type_parameters(p_class->type_parameters);

	if (p_class->extends_used) {
		bool first = true;
		push_text(" Extends ");
		if (!p_class->extends_path.is_empty()) {
			push_text(vformat(R"("%s")", p_class->extends_path));
			first = false;
		}
		for (int i = 0; i < p_class->extends.size(); i++) {
			if (!first) {
				push_text(".");
			} else {
				first = false;
			}
			push_text(p_class->extends[i]->name);
		}
	}

	if (!p_class->used_traits.is_empty()) {
		push_text(" Uses ");
		for (int i = 0; i < p_class->used_traits.size(); i++) {
			if (i > 0) {
				push_text(", ");
			}
			push_text(p_class->used_traits[i].to_string());
		}
	}

	push_line(" :");

	increase_indent();

	for (AnnotationDeclarationNode *E : p_class->annotation_declarations) {
		print_annotation_declaration(E);
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const ClassNode::Member &m = p_class->members[i];

		switch (m.type) {
			case ClassNode::Member::CLASS:
				print_class(m.m_class);
				break;
			case ClassNode::Member::VARIABLE:
				print_variable(m.variable);
				break;
			case ClassNode::Member::CONSTANT:
				print_constant(m.constant);
				break;
			case ClassNode::Member::SIGNAL:
				print_signal(m.signal);
				break;
			case ClassNode::Member::FUNCTION:
				print_function(m.function);
				break;
			case ClassNode::Member::ENUM:
				print_enum(m.m_enum);
				break;
			case ClassNode::Member::ENUM_VALUE:
				break; // Nothing. Will be printed by enum.
			case ClassNode::Member::GROUP:
				break; // Nothing. Groups are only used by inspector.
			case ClassNode::Member::TUPLE:
				print_tuple(m.m_tuple);
				break;
			case ClassNode::Member::TYPE_ALIAS:
				print_type_alias(m.type_alias);
				break;
			case ClassNode::Member::UNDEFINED:
				push_line("<unknown member>");
				break;
		}
	}

	decrease_indent();
}

void BSParser::TreePrinter::print_constant(ConstantNode *p_constant) {
	if (p_constant == nullptr) {
		push_text("<invalid constant>");
		return;
	}
	push_text("Constant ");
	print_identifier(p_constant->identifier);

	increase_indent();

	push_line();
	push_text("= ");
	if (p_constant->initializer == nullptr) {
		push_text("<missing value>");
	} else {
		print_expression(p_constant->initializer);
	}
	decrease_indent();
	push_line();
}

void BSParser::TreePrinter::print_dictionary(DictionaryNode *p_dictionary) {
	if (p_dictionary == nullptr) {
		push_text("<invalid dictionary>");
		return;
	}
	push_line("{");
	increase_indent();
	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		print_expression(p_dictionary->elements[i].key);
		if (p_dictionary->style == DictionaryNode::PYTHON_DICT) {
			push_text(" : ");
		} else {
			push_text(" = ");
		}
		print_expression(p_dictionary->elements[i].value);
		push_line(" ,");
	}
	decrease_indent();
	push_text("}");
}

void BSParser::TreePrinter::print_expression(ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		push_text("<invalid expression>");
		return;
	}
	switch (p_expression->type) {
		case Node::ARRAY:
			print_array(static_cast<ArrayNode *>(p_expression));
			break;
		case Node::ASSIGNMENT:
			print_assignment(static_cast<AssignmentNode *>(p_expression));
			break;
		case Node::AWAIT:
			print_await(static_cast<AwaitNode *>(p_expression));
			break;
		case Node::BINARY_OPERATOR:
			print_binary_op(static_cast<BinaryOpNode *>(p_expression));
			break;
		case Node::CALL:
			print_call(static_cast<CallNode *>(p_expression));
			break;
		case Node::CAST:
			print_cast(static_cast<CastNode *>(p_expression));
			break;
		case Node::DICTIONARY:
			print_dictionary(static_cast<DictionaryNode *>(p_expression));
			break;
		case Node::GET_NODE:
			print_get_node(static_cast<GetNodeNode *>(p_expression));
			break;
		case Node::IDENTIFIER:
			print_identifier(static_cast<IdentifierNode *>(p_expression));
			break;
		case Node::LAMBDA:
			print_lambda(static_cast<LambdaNode *>(p_expression));
			break;
		case Node::LITERAL:
			print_literal(static_cast<LiteralNode *>(p_expression));
			break;
		case Node::PRELOAD:
			print_preload(static_cast<PreloadNode *>(p_expression));
			break;
		case Node::SELF:
			print_self(static_cast<SelfNode *>(p_expression));
			break;
		case Node::SUBSCRIPT:
			print_subscript(static_cast<SubscriptNode *>(p_expression));
			break;
		case Node::TERNARY_OPERATOR:
			print_ternary_op(static_cast<TernaryOpNode *>(p_expression));
			break;
		case Node::TUPLE_LITERAL:
			print_tuple_literal(static_cast<TupleLiteralNode *>(p_expression));
			break;
		case Node::TYPE_TEST:
			print_type_test(static_cast<TypeTestNode *>(p_expression));
			break;
		case Node::UNARY_OPERATOR:
			print_unary_op(static_cast<UnaryOpNode *>(p_expression));
			break;
		default:
			push_text(vformat("<unknown expression %d>", p_expression->type));
			break;
	}
}

void BSParser::TreePrinter::print_enum(EnumNode *p_enum) {
	if (p_enum == nullptr) {
		push_text("<invalid enum>");
		return;
	}
	push_text("Enum ");
	if (p_enum->identifier != nullptr) {
		print_identifier(p_enum->identifier);
	} else {
		push_text("<unnamed>");
	}
	print_type_parameters(p_enum->type_parameters);

	push_line(" {");
	increase_indent();
	for (int i = 0; i < p_enum->values.size(); i++) {
		const EnumNode::Value &item = p_enum->values[i];
		print_identifier(item.identifier);
		push_text(" = ");
		push_text(itos(item.value));
		push_line(" ,");
	}
	for (FunctionNode *function : p_enum->functions) {
		print_function(function);
	}
	decrease_indent();
	push_line("}");
}

void BSParser::TreePrinter::print_for(ForNode *p_for) {
	if (p_for == nullptr) {
		push_text("<invalid for>");
		return;
	}
	push_text("For ");
	print_identifier(p_for->variable);
	push_text(" IN ");
	print_expression(p_for->list);
	push_line(" :");

	increase_indent();

	print_suite(p_for->loop);

	decrease_indent();
}

void BSParser::TreePrinter::print_function(FunctionNode *p_function, const String &p_context) {
	if (p_function == nullptr) {
		push_text("<invalid function>");
		return;
	}
	for (const AnnotationNode *E : p_function->annotations) {
		print_annotation(E);
	}
	if (p_function->is_static) {
		push_text("Static ");
	}
	if (p_function->is_declared_async) {
		push_text("Async ");
	}
	push_text(p_context);
	push_text(" ");
	if (p_function->identifier) {
		print_identifier(p_function->identifier);
	} else {
		push_text("<anonymous>");
	}
	print_type_parameters(p_function->type_parameters);
	push_text("( ");
	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_parameter(p_function->parameters[i]);
	}
	push_line(" ) :");
	increase_indent();
	print_suite(p_function->body);
	decrease_indent();
}

void BSParser::TreePrinter::print_get_node(GetNodeNode *p_get_node) {
	if (p_get_node == nullptr) {
		push_text("<invalid get node>");
		return;
	}
	if (p_get_node->use_dollar) {
		push_text("$");
	}
	push_text(p_get_node->full_path);
}

void BSParser::TreePrinter::print_identifier(IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		push_text("<invalid identifier>");
		return;
	}
	if (p_identifier != nullptr) {
		push_text(p_identifier->name);
	} else {
		push_text("<invalid identifier>");
	}
}

void BSParser::TreePrinter::print_if(IfNode *p_if, bool p_is_elif) {
	if (p_if == nullptr) {
		push_text("<invalid if>");
		return;
	}
	if (p_is_elif) {
		push_text("Elif ");
	} else {
		push_text("If ");
	}
	print_expression(p_if->condition);
	push_line(" :");

	increase_indent();
	print_suite(p_if->true_block);
	decrease_indent();

	if (p_if->false_block == nullptr) {
		return;
	}

	if (BSParser::IfNode *elif = p_if->get_elif()) {
		print_if(elif, true);
	} else {
		push_line("Else :");
		increase_indent();
		print_suite(p_if->false_block);
		decrease_indent();
	}
}

void BSParser::TreePrinter::print_lambda(LambdaNode *p_lambda) {
	if (p_lambda == nullptr) {
		push_text("<invalid lambda>");
		return;
	}
	print_function(p_lambda->function, "Lambda");
	push_text("| captures [ ");
	for (int i = 0; i < p_lambda->captures.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		push_text(String(p_lambda->captures[i]->name));
	}
	push_line(" ]");
}

void BSParser::TreePrinter::print_literal(LiteralNode *p_literal) {
	if (p_literal == nullptr) {
		push_text("<invalid literal>");
		return;
	}
	// Prefix for string types.
	switch (p_literal->value.get_type()) {
		case Variant::NODE_PATH:
			push_text("^\"");
			break;
		case Variant::STRING:
			push_text("\"");
			break;
		case Variant::STRING_NAME:
			push_text("&\"");
			break;
		default:
			break;
	}
	push_text(p_literal->value);
	// Suffix for string types.
	switch (p_literal->value.get_type()) {
		case Variant::NODE_PATH:
		case Variant::STRING:
		case Variant::STRING_NAME:
			push_text("\"");
			break;
		default:
			break;
	}
}

void BSParser::TreePrinter::print_match(MatchNode *p_match) {
	if (p_match == nullptr) {
		push_text("<invalid match>");
		return;
	}
	push_text("Match ");
	print_expression(p_match->test);
	push_line(" :");

	increase_indent();
	for (int i = 0; i < p_match->branches.size(); i++) {
		print_match_branch(p_match->branches[i]);
	}
	decrease_indent();
}

void BSParser::TreePrinter::print_match_branch(MatchBranchNode *p_match_branch) {
	if (p_match_branch == nullptr) {
		push_text("<invalid match branch>");
		return;
	}
	for (int i = 0; i < p_match_branch->patterns.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_match_pattern(p_match_branch->patterns[i]);
	}

	push_line(" :");

	increase_indent();
	print_suite(p_match_branch->block);
	decrease_indent();
}

void BSParser::TreePrinter::print_match_pattern(PatternNode *p_match_pattern) {
	if (p_match_pattern == nullptr) {
		push_text("<invalid match pattern>");
		return;
	}
	switch (p_match_pattern->pattern_type) {
		case PatternNode::PT_LITERAL:
			print_literal(p_match_pattern->literal);
			break;
		case PatternNode::PT_WILDCARD:
			push_text("_");
			break;
		case PatternNode::PT_REST:
			push_text("..");
			break;
		case PatternNode::PT_BIND:
			if (!p_match_pattern->implicit_bind) {
				push_text("Var ");
			}
			print_identifier(p_match_pattern->bind);
			break;
		case PatternNode::PT_EXPRESSION:
			print_expression(p_match_pattern->expression);
			break;
		case PatternNode::PT_ARRAY:
			push_text("[ ");
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				if (i > 0) {
					push_text(" , ");
				}
				print_match_pattern(p_match_pattern->array[i]);
			}
			push_text(" ]");
			break;
		case PatternNode::PT_TUPLE:
			push_text("( ");
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				if (i > 0) {
					push_text(" , ");
				}
				print_match_pattern(p_match_pattern->array[i]);
			}
			push_text(" )");
			break;
		case PatternNode::PT_ENUM_CASE:
			print_type(p_match_pattern->case_type);
			push_text("( ");
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				if (i > 0) {
					push_text(" , ");
				}
				print_match_pattern(p_match_pattern->array[i]);
			}
			push_text(" )");
			break;
		case PatternNode::PT_DICTIONARY:
			push_text("{ ");
			for (int i = 0; i < p_match_pattern->dictionary.size(); i++) {
				if (i > 0) {
					push_text(" , ");
				}
				if (p_match_pattern->dictionary[i].key != nullptr) {
					// Key can be null for rest pattern.
					print_expression(p_match_pattern->dictionary[i].key);
					push_text(" : ");
				}
				print_match_pattern(p_match_pattern->dictionary[i].value_pattern);
			}
			push_text(" }");
			break;
	}
}

void BSParser::TreePrinter::print_parameter(ParameterNode *p_parameter) {
	if (p_parameter == nullptr) {
		push_text("<invalid parameter>");
		return;
	}
	print_identifier(p_parameter->identifier);
	if (p_parameter->datatype_specifier != nullptr) {
		push_text(" : ");
		print_type(p_parameter->datatype_specifier);
	}
	if (p_parameter->initializer != nullptr) {
		push_text(" = ");
		print_expression(p_parameter->initializer);
	}
}

void BSParser::TreePrinter::print_preload(PreloadNode *p_preload) {
	if (p_preload == nullptr) {
		push_text("<invalid preload>");
		return;
	}
	push_text(R"(Preload ( ")");
	push_text(p_preload->resolved_path);
	push_text(R"(" )");
}

void BSParser::TreePrinter::print_return(ReturnNode *p_return) {
	if (p_return == nullptr) {
		push_text("<invalid return>");
		return;
	}
	push_text("Return");
	if (p_return->return_value != nullptr) {
		push_text(" ");
		print_expression(p_return->return_value);
	}
	push_line();
}

void BSParser::TreePrinter::print_self(SelfNode *p_self) {
	if (p_self == nullptr) {
		push_text("<invalid self>");
		return;
	}
	push_text("Self(");
	if (p_self->current_class->identifier != nullptr) {
		print_identifier(p_self->current_class->identifier);
	} else {
		push_text("<main class>");
	}
	push_text(")");
}

void BSParser::TreePrinter::print_signal(SignalNode *p_signal) {
	if (p_signal == nullptr) {
		push_text("<invalid signal>");
		return;
	}
	push_text("Signal ");
	print_identifier(p_signal->identifier);
	push_text("( ");
	for (int i = 0; i < p_signal->parameters.size(); i++) {
		print_parameter(p_signal->parameters[i]);
	}
	push_line(" )");
}

void BSParser::TreePrinter::print_subscript(SubscriptNode *p_subscript) {
	if (p_subscript == nullptr) {
		push_text("<invalid subscript>");
		return;
	}
	print_expression(p_subscript->base);
	if (p_subscript->is_attribute) {
		push_text(".");
		print_identifier(p_subscript->attribute);
	} else if (p_subscript->is_tuple_index) {
		push_text(".");
		print_expression(p_subscript->index);
	} else {
		push_text("[ ");
		print_expression(p_subscript->index);
		push_text(" ]");
	}
}

void BSParser::TreePrinter::print_statement(Node *p_statement) {
	if (p_statement == nullptr) {
		push_text("<invalid statement>");
		return;
	}
	switch (p_statement->type) {
		case Node::ASSERT:
			print_assert(static_cast<AssertNode *>(p_statement));
			break;
		case Node::VARIABLE:
			print_variable(static_cast<VariableNode *>(p_statement));
			break;
		case Node::VARIABLE_DESTRUCTURE:
			print_variable_destructure(static_cast<VariableDestructureNode *>(p_statement));
			break;
		case Node::CONSTANT:
			print_constant(static_cast<ConstantNode *>(p_statement));
			break;
		case Node::IF:
			print_if(static_cast<IfNode *>(p_statement));
			break;
		case Node::FOR:
			print_for(static_cast<ForNode *>(p_statement));
			break;
		case Node::WHILE:
			print_while(static_cast<WhileNode *>(p_statement));
			break;
		case Node::MATCH:
			print_match(static_cast<MatchNode *>(p_statement));
			break;
		case Node::RETURN:
			print_return(static_cast<ReturnNode *>(p_statement));
			break;
		case Node::BREAK:
			push_line("Break");
			break;
		case Node::CONTINUE:
			push_line("Continue");
			break;
		case Node::PASS:
			push_line("Pass");
			break;
		case Node::BREAKPOINT:
			push_line("Breakpoint");
			break;
		case Node::ASSIGNMENT:
			print_assignment(static_cast<AssignmentNode *>(p_statement));
			break;
		default:
			if (p_statement->is_expression()) {
				print_expression(static_cast<ExpressionNode *>(p_statement));
				push_line();
			} else {
				push_line(vformat("<unknown statement %d>", p_statement->type));
			}
			break;
	}
}

void BSParser::TreePrinter::print_suite(SuiteNode *p_suite) {
	if (p_suite == nullptr) {
		push_text("<invalid suite>");
		return;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		print_statement(p_suite->statements[i]);
	}
}

void BSParser::TreePrinter::print_ternary_op(TernaryOpNode *p_ternary_op) {
	if (p_ternary_op == nullptr) {
		push_text("<invalid ternary op>");
		return;
	}
	// Surround in parenthesis for disambiguation.
	push_text("(");
	print_expression(p_ternary_op->true_expr);
	push_text(") IF (");
	print_expression(p_ternary_op->condition);
	push_text(") ELSE (");
	print_expression(p_ternary_op->false_expr);
	push_text(")");
}

void BSParser::TreePrinter::print_tuple(TupleNode *p_tuple) {
	if (p_tuple == nullptr) {
		push_text("<invalid tuple>");
		return;
	}
	push_text("Tuple ");
	if (p_tuple->identifier != nullptr) {
		print_identifier(p_tuple->identifier);
	} else {
		push_text("<unnamed>");
	}

	push_line(" (");
	increase_indent();
	for (int i = 0; i < p_tuple->fields.size(); i++) {
		const TupleNode::Field &field = p_tuple->fields[i];
		if (field.identifier != nullptr) {
			print_identifier(field.identifier);
			push_text(" : ");
		}
		if (field.type != nullptr) {
			print_type(field.type);
		} else {
			push_text("<missing type>");
		}
		push_line(" ,");
	}
	decrease_indent();
	push_line(")");
}

void BSParser::TreePrinter::print_tuple_literal(TupleLiteralNode *p_tuple_literal) {
	if (p_tuple_literal == nullptr) {
		push_text("<invalid tuple literal>");
		return;
	}
	push_text("( ");
	for (int i = 0; i < p_tuple_literal->elements.size(); i++) {
		if (i > 0) {
			push_text(" , ");
		}
		print_expression(p_tuple_literal->elements[i]);
	}
	push_text(" )");
}

void BSParser::TreePrinter::print_type_alias(TypeAliasNode *p_type_alias) {
	if (p_type_alias == nullptr) {
		push_text("<invalid type alias>");
		return;
	}
	push_text("Type Alias ");
	if (p_type_alias->identifier != nullptr) {
		print_identifier(p_type_alias->identifier);
	}
	push_text(" = ");
	if (p_type_alias->aliased_type != nullptr) {
		print_type(p_type_alias->aliased_type);
	}
	push_line();
}

void BSParser::TreePrinter::print_type(TypeNode *p_type) {
	if (p_type == nullptr) {
		push_text("<invalid type>");
		return;
	}
	if (p_type->is_union) {
		for (int i = 0; i < p_type->union_member_types.size(); i++) {
			if (i > 0) {
				push_text(" | ");
			}
			print_type(p_type->union_member_types[i]);
		}
		return;
	}
	if (p_type->is_tuple) {
		push_text("( ");
		for (int i = 0; i < p_type->tuple_element_types.size(); i++) {
			if (i > 0) {
				push_text(" , ");
			}
			print_type(p_type->tuple_element_types[i]);
		}
		push_text(" )");
		if (p_type->is_nullable) {
			push_text("?");
		}
		return;
	}
	if (p_type->type_chain.is_empty()) {
		push_text("Void");
	} else {
		for (int i = 0; i < p_type->type_chain.size(); i++) {
			if (i > 0) {
				push_text(".");
			}
			print_identifier(p_type->type_chain[i]);
		}
	}
	if (p_type->is_nullable) {
		push_text("?");
	}
}

void BSParser::TreePrinter::print_type_parameters(const Vector<TypeParameterNode *> &p_type_parameters) {
	if (p_type_parameters.is_empty()) {
		return;
	}
	push_text("[");
	for (int i = 0; i < p_type_parameters.size(); i++) {
		if (i > 0) {
			push_text(", ");
		}
		const TypeParameterNode *type_parameter = p_type_parameters[i];
		if (type_parameter->identifier != nullptr) {
			push_text(type_parameter->identifier->name);
		} else {
			push_text("<unnamed>");
		}
		if (type_parameter->bound != nullptr) {
			push_text(": ");
			print_type(type_parameter->bound);
		}
	}
	push_text("]");
}

void BSParser::TreePrinter::print_type_test(TypeTestNode *p_test) {
	if (p_test == nullptr) {
		push_text("<invalid type test>");
		return;
	}
	print_expression(p_test->operand);
	push_text(" IS ");
	print_type(p_test->test_type);
	if (p_test->case_binds.is_empty()) {
		return;
	}
	push_text("(");
	for (int i = 0; i < p_test->case_binds.size(); i++) {
		if (i > 0) {
			push_text(", ");
		}
		IdentifierNode *bind = p_test->case_binds[i];
		push_text(bind != nullptr ? String(bind->name) : String("_"));
	}
	push_text(")");
}

void BSParser::TreePrinter::print_unary_op(UnaryOpNode *p_unary_op) {
	if (p_unary_op == nullptr) {
		push_text("<invalid unary op>");
		return;
	}
	// Surround in parenthesis for disambiguation.
	push_text("(");
	switch (p_unary_op->operation) {
		case UnaryOpNode::OP_POSITIVE:
			push_text("+");
			break;
		case UnaryOpNode::OP_NEGATIVE:
			push_text("-");
			break;
		case UnaryOpNode::OP_LOGIC_NOT:
			push_text("NOT");
			break;
		case UnaryOpNode::OP_COMPLEMENT:
			push_text("~");
			break;
	}
	print_expression(p_unary_op->operand);
	// Surround in parenthesis for disambiguation.
	push_text(")");
}

void BSParser::TreePrinter::print_variable(VariableNode *p_variable) {
	if (p_variable == nullptr) {
		push_text("<invalid variable>");
		return;
	}
	for (const AnnotationNode *E : p_variable->annotations) {
		print_annotation(E);
	}

	if (p_variable->is_static) {
		push_text("Static ");
	}
	push_text("Variable ");
	print_identifier(p_variable->identifier);

	push_text(" : ");
	if (p_variable->datatype_specifier != nullptr) {
		print_type(p_variable->datatype_specifier);
	} else if (p_variable->infer_datatype) {
		push_text("<inferred type>");
	} else {
		push_text("Variant");
	}

	increase_indent();

	push_line();
	push_text("= ");
	if (p_variable->initializer == nullptr) {
		push_text("<default value>");
	} else {
		print_expression(p_variable->initializer);
	}
	push_line();

	if (p_variable->property != VariableNode::PROP_NONE) {
		if (p_variable->getter != nullptr) {
			push_text("Get");
			if (p_variable->property == VariableNode::PROP_INLINE) {
				push_line(":");
				increase_indent();
				print_suite(p_variable->getter->body);
				decrease_indent();
			} else {
				push_line(" =");
				increase_indent();
				print_identifier(p_variable->getter_pointer);
				push_line();
				decrease_indent();
			}
		}
		if (p_variable->setter != nullptr) {
			push_text("Set (");
			if (p_variable->property == VariableNode::PROP_INLINE) {
				if (p_variable->setter_parameter != nullptr) {
					print_identifier(p_variable->setter_parameter);
				} else {
					push_text("<missing>");
				}
				push_line("):");
				increase_indent();
				print_suite(p_variable->setter->body);
				decrease_indent();
			} else {
				push_line(" =");
				increase_indent();
				print_identifier(p_variable->setter_pointer);
				push_line();
				decrease_indent();
			}
		}
	}

	decrease_indent();
	push_line();
}

void BSParser::TreePrinter::print_variable_destructure(VariableDestructureNode *p_destructure) {
	if (p_destructure == nullptr) {
		push_text("<invalid variable destructure>");
		return;
	}
	for (const AnnotationNode *E : p_destructure->annotations) {
		print_annotation(E);
	}

	push_text(p_destructure->is_const ? "Constant Destructure (" : "Variable Destructure (");
	for (int i = 0; i < p_destructure->bindings.size(); i++) {
		if (i > 0) {
			push_text(", ");
		}
		if (p_destructure->bindings[i] == nullptr) {
			push_text("_");
		} else {
			print_identifier(p_destructure->bindings[i]->identifier);
		}
	}
	push_text(")");

	increase_indent();
	push_line();
	push_text("= ");
	if (p_destructure->initializer == nullptr) {
		push_text("<missing>");
	} else {
		print_expression(p_destructure->initializer);
	}
	push_line();
	decrease_indent();
	push_line();
}

void BSParser::TreePrinter::print_while(WhileNode *p_while) {
	if (p_while == nullptr) {
		push_text("<invalid while>");
		return;
	}
	push_text("While ");
	print_expression(p_while->condition);
	push_line(" :");

	increase_indent();
	print_suite(p_while->loop);
	decrease_indent();
}

String BSParser::TreePrinter::render_tree(const BSParser &p_parser) {
	ClassNode *class_tree = p_parser.get_tree();
	ERR_FAIL_NULL_V_MSG(class_tree, String(), "Parse the code before printing the parse tree.");

	if (p_parser.is_tool()) {
		push_line("@tool");
	}
	if (!class_tree->icon_path.is_empty()) {
		push_text(R"(@icon (")");
		push_text(class_tree->icon_path);
		push_line("\")");
	}
	print_class(class_tree);

	return String(printed);
}

void BSParser::TreePrinter::print_tree(const BSParser &p_parser) {
	print_line(render_tree(p_parser));
}

#endif // DEBUG_ENABLED

} // namespace barista_script
