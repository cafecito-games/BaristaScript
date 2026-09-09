/**************************************************************************/
/*  bs_analyzer.cpp                                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

/**************************************************************************/
/*  bs_analyzer.cpp                                                       */
/*                                                                        */
/*  M3 analyzer port (issue #43/#57/#60) @ Foundry c9d5e35. Inheritance,  */
/*  interface, body fold (#49), declaration commit (#52/#58), call/match/ */
/*  flow (#61), final DA, CallSiteValidationContext / connect-callable,   */
/*  unused surface, ENUM_CASE / `.Case` / exhaustiveness, Callable.bind,  */
/*  pending-warning finalize, trait conformance (#60), same-file extends  */
/*  + CLASS inheritance member walk, cycle-safe walks (#110), lambda      */
/*  capture + compound-assign restore, get_operation_type,                */
/*  resolve_class_member same-parser depth, reduce_await + MISSING_AWAIT / */
/*  REDUNDANT_AWAIT (#60), class-phase INTERFACE/BODY foreign failure     */
/*  recording and dependent replay (#60 residual after #118), Coroutine[T]*/
/*  annotation decode in datatype_from_type_node (#60 residual), direct   */
/*  async-call wrap + mark_coroutine_handle_capture (#60 residual).       */
/*  Non-generic SelfFieldLeg + Self-contract RETURN assign/return (#60). */
/*  Gradual Self-union admission + self_free union members (#60 residual). */
/*  complete_self_referential_enum_type + specialize helpers (#60).      */
/*  Deliberate non-ports: NumericType / fs_numeric_ops / integer suffixes */
/*  are deleted by D1; fs_builtin_types registration and its JsonResult  */
/*  generic return hint require the M5 builtin generic source surface;    */
/*  runtime Function witnesses and compiler open-Self lowering are M4/M5.*/
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_builtin_sources.h"
#include "bs_cache.h"
#include "bs_core_constants.h"
#include "bs_declaration_index.h"
#include "bs_diagnostic_names.h"
#include "bs_global_class.h"
#include "bs_native_db.h"
#include "bs_script_server.h"
#include "bs_trait_utils.h"
#include "bs_utility_functions.h"
#include "bs_warning.h"

namespace barista_script {

namespace {

static String _class_script_path_for_foreign_resolve(const BSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return String();
	}
	const BSParser::DataType class_type = p_class->get_datatype();
	if (!class_type.script_path.is_empty()) {
		return class_type.script_path;
	}
	if (!p_class->fqcn.is_empty()) {
		return p_class->fqcn.get_slice("::", 0);
	}
	return String();
}

// Foundry `_dependency_error_suffix` @ c9d5e35 (`fs_analyzer.cpp` ~1499): body/trait foreign
// failure messages name the declaring file and first owner-local error when available.
static String _dependency_error_suffix(const char *p_noun, const String &p_path, BSParser *p_dependency_parser, int p_first_error_index) {
	String first_error;
	if (p_dependency_parser != nullptr) {
		int index = 0;
		for (const BSParser::ParserError &error : p_dependency_parser->get_errors()) {
			if (index++ < p_first_error_index) {
				continue;
			}
			first_error = vformat("line %d: %s", error.line, error.message);
			break;
		}
	}

	const String script_path = bs_diagnostic_file_reference(p_path);
	if (script_path.is_empty()) {
		if (first_error.is_empty()) {
			return String();
		}
		return vformat("The %s has errors, the first at %s", p_noun, first_error);
	}
	if (first_error.is_empty()) {
		return vformat(R"(The %s is declared in "%s".)", p_noun, script_path);
	}
	return vformat(R"(The %s is declared in "%s", which has errors, the first at %s)", p_noun, script_path, first_error);
}

// Foundry coroutine_result_is_void @ c9d5e35 (~1664): True when a coroutine's phantom result is
// hard `void` (BUILTIN NIL in container_element_types[0]). Root-position discards of
// Coroutine[void] are intentional fire-and-forget launches — no result exists to lose — so
// MISSING_AWAIT skips them. A missing element (lossy signature boundary) or any non-void result
// still warns.
bool coroutine_result_is_void(const BSParser::DataType &p_type) {
	if (!p_type.is_coroutine || !p_type.has_container_element_type(0)) {
		return false;
	}
	const BSParser::DataType result = p_type.get_container_element_type(0);
	return result.kind == BSParser::DataType::BUILTIN && result.builtin_type == Variant::NIL;
}

// Foundry fs_tagged_union_case_singleton @ c9d5e35: payload-less cases erase to a read-only
// `[tag]` Array singleton. Full bs_tagged_union.h port remains available for later TUs.
Variant _tagged_union_case_singleton(int64_t p_tag) {
	Array value;
	value.push_back(p_tag);
	value.make_read_only();
	return value;
}

String _operator_name(Variant::Operator p_op) {
	// Mirrors core Variant::get_operator_name (@ c9d5e35) for diagnostics.
	switch (p_op) {
		case Variant::OP_EQUAL:
			return "==";
		case Variant::OP_NOT_EQUAL:
			return "!=";
		case Variant::OP_LESS:
			return "<";
		case Variant::OP_LESS_EQUAL:
			return "<=";
		case Variant::OP_GREATER:
			return ">";
		case Variant::OP_GREATER_EQUAL:
			return ">=";
		case Variant::OP_ADD:
			return "+";
		case Variant::OP_SUBTRACT:
			return "-";
		case Variant::OP_MULTIPLY:
			return "*";
		case Variant::OP_DIVIDE:
			return "/";
		case Variant::OP_NEGATE:
			return "-";
		case Variant::OP_POSITIVE:
			return "+";
		case Variant::OP_MODULE:
			return "%";
		case Variant::OP_POWER:
			return "**";
		case Variant::OP_SHIFT_LEFT:
			return "<<";
		case Variant::OP_SHIFT_RIGHT:
			return ">>";
		case Variant::OP_BIT_AND:
			return "&";
		case Variant::OP_BIT_OR:
			return "|";
		case Variant::OP_BIT_XOR:
			return "^";
		case Variant::OP_BIT_NEGATE:
			return "~";
		case Variant::OP_AND:
			return "and";
		case Variant::OP_OR:
			return "or";
		case Variant::OP_XOR:
			return "xor";
		case Variant::OP_NOT:
			return "not";
		case Variant::OP_IN:
			return "in";
		default:
			return String::num_int64((int64_t)p_op);
	}
}

bool _is_integer_overflow_mul(int64_t a, int64_t b) {
	if (a == 0 || b == 0) {
		return false;
	}
	if (a == INT64_MIN && b == -1) {
		return true;
	}
	if (b == INT64_MIN && a == -1) {
		return true;
	}
	const int64_t result = a * b;
	return result / a != b;
}

bool _checked_int_binary(Variant::Operator p_op, const Variant &p_left, const Variant &p_right, Variant &r_result, String &r_error) {
	if (p_left.get_type() != Variant::INT || p_right.get_type() != Variant::INT) {
		return false;
	}
	const int64_t left = p_left;
	const int64_t right = p_right;
	switch (p_op) {
		case Variant::OP_ADD: {
			if ((right > 0 && left > INT64_MAX - right) || (right < 0 && left < INT64_MIN - right)) {
				r_error = "Integer addition overflow.";
				return false;
			}
			r_result = left + right;
			return true;
		}
		case Variant::OP_SUBTRACT: {
			if ((right < 0 && left > INT64_MAX + right) || (right > 0 && left < INT64_MIN + right)) {
				r_error = "Integer subtraction overflow.";
				return false;
			}
			r_result = left - right;
			return true;
		}
		case Variant::OP_MULTIPLY: {
			if (_is_integer_overflow_mul(left, right)) {
				r_error = "Integer multiplication overflow.";
				return false;
			}
			r_result = left * right;
			return true;
		}
		case Variant::OP_NEGATE: {
			if (left == INT64_MIN) {
				r_error = "Integer negation overflow.";
				return false;
			}
			r_result = -left;
			return true;
		}
		default:
			return false;
	}
}

} // namespace

// Foundry make_coroutine_type @ c9d5e35 (~1646): wrap result T as Coroutine[T]. Principal identity
// is the native BSFunctionState skin; is_coroutine discriminates await / missing-await; the phantom
// result type lives in container_element_types[0]. Shared with call_validation via bs_analyzer.h.
BSParser::DataType make_coroutine_type(const BSParser::DataType &p_result_type) {
	BSParser::DataType type;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = BSParser::DataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = SNAME("BSFunctionState");
	type.is_coroutine = true;
	BSParser::DataType result_type = p_result_type;
	result_type.is_constant = false;
	result_type.is_meta_type = false;
	type.set_container_element_type(0, result_type);
	return type;
}

namespace {

// Foundry mark_coroutine_handle_capture @ c9d5e35 (~1680): a coroutine call whose live
// BSFunctionState handle is captured into a hard Coroutine[T] slot is meant to be held and
// awaited later, not a forgotten await. Mark such a call so the compiler can emit
// OPCODE_CALL_ASYNC. Recurse through cast / ternary wrappers; do not follow await (unwraps to T).
void mark_coroutine_handle_capture_impl(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_target_type) {
	if (p_expression == nullptr || !p_target_type.is_coroutine || !p_target_type.is_hard_type()) {
		return;
	}
	switch (p_expression->type) {
		case BSParser::Node::CALL: {
			BSParser::CallNode *call = static_cast<BSParser::CallNode *>(p_expression);
			if (call->get_datatype().is_coroutine) {
				call->is_coroutine_handle_capture = true;
			}
		} break;
		case BSParser::Node::CAST: {
			mark_coroutine_handle_capture_impl(static_cast<BSParser::CastNode *>(p_expression)->operand, p_target_type);
		} break;
		case BSParser::Node::TERNARY_OPERATOR: {
			BSParser::TernaryOpNode *ternary = static_cast<BSParser::TernaryOpNode *>(p_expression);
			mark_coroutine_handle_capture_impl(ternary->true_expr, p_target_type);
			mark_coroutine_handle_capture_impl(ternary->false_expr, p_target_type);
		} break;
		default:
			break;
	}
}

} // namespace

void BSAnalyzer::mark_coroutine_handle_capture(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_target_type) {
	mark_coroutine_handle_capture_impl(p_expression, p_target_type);
}

String &BSAnalyzer::bootstrap_root_storage() {
	static String *root = nullptr;
	if (root == nullptr) {
		root = memnew(String);
	}
	return *root;
}

BSAnalyzer::BSAnalyzer(BSParser *p_parser) :
		conformance_visibility(this),
		parser(p_parser),
		call_site_validation(this),
		flow_finality(this) {
	read_strict_settings();
}

void BSAnalyzer::read_strict_settings() {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return;
	}
	strict_null_checks = bool(settings->get_setting("debug/barista_script/analysis/strict_null_checks", false));
	strict_dynamic_checks = bool(settings->get_setting("debug/barista_script/analysis/strict_dynamic_checks", false));
}

void BSAnalyzer::mark_phase(AnalyzerPhase p_phase) {
	if ((int)p_phase > (int)highest_completed_phase) {
		highest_completed_phase = p_phase;
	}
}

void BSAnalyzer::push_error(const String &p_message, const BSParser::Node *p_origin) {
	ERR_FAIL_NULL(parser);
	mark_node_unsafe(p_origin);
	parser->push_error(p_message, p_origin);
}

void BSAnalyzer::mark_node_unsafe(const BSParser::Node *p_node) {
#ifdef DEBUG_ENABLED
	if (parser == nullptr || p_node == nullptr) {
		return;
	}
	for (int i = p_node->start_line; i <= p_node->end_line; i++) {
		parser->unsafe_lines.insert(i);
	}
#else
	(void)p_node;
#endif
}

#ifdef DEBUG_ENABLED
void BSAnalyzer::push_warning(const BSParser::Node *p_origin, BSWarning::Code p_code, const Vector<String> &p_symbols) {
	ERR_FAIL_NULL(parser);
	if (p_origin == nullptr) {
		return;
	}
	parser->push_warning(p_origin, p_code, p_symbols);
}
#endif

bool BSAnalyzer::errors_are_only_m5_deferred() const {
	ERR_FAIL_COND_V(parser == nullptr, false);
	if (parser->get_errors().is_empty()) {
		return false;
	}
	return errors_from_index_are_only_m5_deferred(0);
}

bool BSAnalyzer::errors_from_index_are_only_m5_deferred(int p_from_index) const {
	ERR_FAIL_COND_V(parser == nullptr, false);
	int index = 0;
	bool saw_any = false;
	for (const BSParser::ParserError &error : parser->get_errors()) {
		if (index++ < p_from_index) {
			continue;
		}
		saw_any = true;
		if (!error.message.contains("not available until M5")) {
			return false;
		}
	}
	return saw_any;
}

BSParser::FunctionNode *BSAnalyzer::find_class_function(BSParser::ClassNode *p_class, const StringName &p_name) const {
	if (p_class == nullptr || p_name == StringName()) {
		return nullptr;
	}
	// Soft mutual path-extends may install CLASS loops for registration; stop identity revisits.
	HashSet<const BSParser::ClassNode *> visited;
	for (BSParser::ClassNode *lookup = p_class; lookup != nullptr; lookup = lookup->base_type.class_type) {
		if (visited.has(lookup)) {
			break;
		}
		visited.insert(lookup);
		if (!lookup->has_member(p_name)) {
			continue;
		}
		// Foundry @ c9d5e35: resolve_class_member before reading the FUNCTION node / signature.
		// const_cast: find sites always run during analysis on a live analyzer.
		const_cast<BSAnalyzer *>(this)->resolve_class_member(lookup, p_name);
		const BSParser::ClassNode::Member member = lookup->get_member(p_name);
		if (member.type == BSParser::ClassNode::Member::FUNCTION) {
			return member.function;
		}
		// An ordinary non-function declaration claims the name; do not resurrect a same-named
		// function further up the chain (Foundry ordinary_member_name_found @ c9d5e35).
		return nullptr;
	}
	return nullptr;
}

bool BSAnalyzer::is_bootstrap_path_allowed(const String &p_path) {
	const String &bootstrap_allowed_dependency_root = bootstrap_root_storage();
	if (bootstrap_allowed_dependency_root.is_empty()) {
		return true;
	}
	const String root = bootstrap_allowed_dependency_root.simplify_path();
	const String path = p_path.simplify_path();
	if (path == root) {
		return true;
	}
	const String prefix = root.ends_with("/") ? root : root + String("/");
	return path.begins_with(prefix);
}

void BSAnalyzer::set_bootstrap_allowed_dependency_root(const String &p_root) {
	bootstrap_root_storage() = p_root.simplify_path();
}

String BSAnalyzer::get_bootstrap_allowed_dependency_root() {
	return bootstrap_root_storage();
}

BSParser::DataType BSAnalyzer::type_from_variant(const Variant &p_value) {
	BSParser::DataType type;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = p_value.get_type();
	type.type_source = BSParser::DataType::ANNOTATED_INFERRED;
	type.is_constant = true;
	return type;
}

BSParser::DataType BSAnalyzer::type_from_property(const PropertyInfo &p_property, bool p_is_arg, bool p_is_readonly) const {
	// D1-trimmed decode of Foundry FSAnalyzer::type_from_property (@ c9d5e35): carrier-only
	// PropertyInfo → DataType for MethodInfo call validation. Width/signedness metadata is never
	// consulted; coroutine / Callable-signature hint decoding remains follow-up under #60.
	BSParser::DataType result;
	result.is_read_only = p_is_readonly;
	result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	if (p_property.type == Variant::NIL && (p_is_arg || (p_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT))) {
		result.kind = BSParser::DataType::VARIANT;
		return result;
	}
	result.builtin_type = p_property.type;
	if (p_property.type == Variant::OBJECT) {
		StringName class_name = p_property.class_name;
		if (String(class_name).ends_with("?")) {
			String nullable_class_name = class_name;
			nullable_class_name = nullable_class_name.substr(0, nullable_class_name.length() - 1);
			class_name = nullable_class_name;
			result.is_nullable = true;
		}
		if (ScriptServer::is_global_class(class_name)) {
			result.kind = BSParser::DataType::SCRIPT;
			result.script_path = ScriptServer::get_global_class_path(class_name);
			result.native_type = ScriptServer::get_global_class_native_base(class_name);
		} else {
			result.kind = BSParser::DataType::NATIVE;
			result.native_type = class_name == StringName() ? StringName("Object") : class_name;
		}
	} else {
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = p_property.type;
	}
	return result;
}

void BSAnalyzer::validate_bootstrap_namespace_imports() {
	ERR_FAIL_NULL(parser);
	BSParser::ClassNode *head = parser->get_tree();
	if (head == nullptr) {
		return;
	}
	for (int i = 0; i < head->imports.size(); i++) {
		validate_bootstrap_namespace_import(head->imports[i]);
	}
}

bool BSAnalyzer::validate_bootstrap_namespace_import(const String &p_import) {
	// Foundry FSAnalyzer::validate_bootstrap_namespace_import @ c9d5e35 — explicit import of a
	// namespace whose only relevant providers lie outside the bootstrap root is analyzer-owned (#52/#58).
	if (bootstrap_root_storage().is_empty()) {
		return true;
	}
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	bool found_namespace_member = false;
	const String namespace_prefix = p_import + String(".");

	List<StringName> global_classes;
	ScriptServer::get_global_class_list(&global_classes);
	for (const StringName &global_class : global_classes) {
		if (!String(global_class).begins_with(namespace_prefix)) {
			continue;
		}
		found_namespace_member = true;
		const String path = ScriptServer::get_global_class_path(global_class);
		if (!is_bootstrap_path_allowed(path)) {
			push_error(vformat(R"(Build task bootstrap cannot import namespace "%s"; global class "%s" from "%s" is outside the provider bootstrap root "%s".)",
							   p_import, global_class, path, bootstrap_root_storage()),
					parser->get_tree());
			return false;
		}
	}

	if (language != nullptr) {
		const Vector<BSDeclarationRecord> records = language->get_declaration_index().get_records();
		for (int i = 0; i < records.size(); i++) {
			const BSDeclarationRecord &record = records[i];
			const bool in_namespace = record.namespace_name == p_import || record.qualified_name.begins_with(namespace_prefix);
			if (!in_namespace) {
				continue;
			}
			found_namespace_member = true;
			if (!is_bootstrap_path_allowed(record.path)) {
				if (!record.global_annotations.is_empty()) {
					push_error(vformat(R"(Build task bootstrap cannot import namespace "%s"; annotation "%s" from "%s" is outside the provider bootstrap root "%s".)",
									   p_import, record.global_annotations[0], record.path, bootstrap_root_storage()),
							parser->get_tree());
				} else {
					push_error(vformat(R"(Build task bootstrap cannot import namespace "%s"; declaration "%s" from "%s" is outside the provider bootstrap root "%s".)",
									   p_import, record.qualified_name, record.path, bootstrap_root_storage()),
							parser->get_tree());
				}
				return false;
			}
		}

		for (const String &conformance_path : language->get_conformance_files_in_namespace(p_import)) {
			found_namespace_member = true;
			if (!is_bootstrap_path_allowed(conformance_path)) {
				push_error(vformat(R"(Build task bootstrap cannot import namespace "%s"; retroactive conformance from "%s" is outside the provider bootstrap root "%s".)",
								   p_import, conformance_path, bootstrap_root_storage()),
						parser->get_tree());
				return false;
			}
		}
	}

	if (!found_namespace_member) {
		push_error(vformat(R"(Could not find imported namespace "%s".)", p_import), parser->get_tree());
		return false;
	}
	return true;
}

Error BSAnalyzer::run_phase_preflight() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	if (!parser->get_errors().is_empty()) {
		return ERR_PARSE_ERROR;
	}
	validate_bootstrap_namespace_imports();
	validate_annotation_declarations();
	mark_phase(AnalyzerPhase::PREFLIGHT);
	mark_phase(AnalyzerPhase::DEPENDENCY_PARSE_AVAILABILITY);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

void BSAnalyzer::resolve_class_inheritance(BSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}

	// Foundry resolves the class itself before nested members (`resolve_class_inheritance(..., true)`),
	// so an outer is already set when a nested `extends Sibling` walks scope via `outer`.
	if (p_class->base_type.is_resolving()) {
		push_error(vformat(R"(Could not resolve class "%s": Cyclic reference.)",
						   p_class->identifier != nullptr ? String(p_class->identifier->name) : String("<main>")),
				p_class);
		return;
	}
	if (p_class->base_type.is_set()) {
		return;
	}

	BSParser::DataType resolving_datatype;
	resolving_datatype.kind = BSParser::DataType::RESOLVING;
	p_class->base_type = resolving_datatype;

	BSParser::DataType class_meta;
	class_meta.is_constant = true;
	class_meta.is_meta_type = true;
	class_meta.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	class_meta.kind = BSParser::DataType::CLASS;
	class_meta.class_type = p_class;
	class_meta.script_path = parser != nullptr ? parser->script_path : String();
	class_meta.builtin_type = Variant::OBJECT;
	p_class->set_datatype(class_meta);

	BSParser::DataType result;
	if (!p_class->extends_used) {
		result.kind = BSParser::DataType::NATIVE;
		result.native_type = SNAME("RefCounted");
		result.builtin_type = Variant::OBJECT;
		result.type_source = BSParser::DataType::ANNOTATED_INFERRED;
	} else if (!p_class->extends_path.is_empty()) {
		String path = p_class->extends_path.strip_edges();
		if (path.is_relative_path() && parser != nullptr && !parser->script_path.is_empty()) {
			path = parser->script_path.get_base_dir().path_join(path).simplify_path();
		}
		path = BaristaScript::canonicalize_path(path);
		if (!is_bootstrap_path_allowed(path) && path.begins_with("res://")) {
			push_error(vformat(R"(Cannot depend on "%s": path is outside the bootstrap allowed dependency root.)", path), p_class);
		}
		Error err = OK;
		Ref<BSParserRef> base_ref = BSCache::get_parser(path, BSParserRef::INHERITANCE_SOLVED, err, parser != nullptr ? parser->script_path : String());
		if (base_ref.is_null() || err != OK || base_ref->get_parser() == nullptr || base_ref->get_parser()->get_tree() == nullptr) {
			push_error(vformat(R"(Could not resolve base script "%s".)", path), p_class);
			p_class->base_type = BSParser::DataType();
			return;
		}
		BSParser::ClassNode *base_class = base_ref->get_parser()->get_tree();
		result.kind = BSParser::DataType::CLASS;
		result.class_type = base_class;
		result.script_path = path;
		result.native_type = base_class->base_type.native_type;
		result.builtin_type = Variant::OBJECT;
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	} else if (p_class->extends.is_empty()) {
		push_error("Extends used without a base type.", p_class);
		p_class->base_type = BSParser::DataType();
		return;
	} else {
		BSParser::IdentifierNode *first_id = p_class->extends[0];
		const StringName first = first_id->name;
		int extends_index = 1;
		bool found = false;

		// D7: native names remain flat — only a single-identifier extends can be a native class.
		if (p_class->extends.size() == 1 && ClassDB::class_exists(first)) {
			result.kind = BSParser::DataType::NATIVE;
			result.native_type = first;
			result.builtin_type = Variant::OBJECT;
			result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			found = true;
		}

		if (!found) {
			String qualified;
			for (int i = 0; i < p_class->extends.size(); i++) {
				if (i > 0) {
					qualified += ".";
				}
				qualified += String(p_class->extends[i]->name);
			}
			if (ScriptServer::is_global_class(StringName(qualified))) {
				const String path = ScriptServer::get_global_class_path(StringName(qualified));
				if (!is_bootstrap_path_allowed(path)) {
					push_error(vformat(R"(Cannot depend on global class "%s" at "%s": path is outside the bootstrap allowed dependency root.)", qualified, path), p_class);
				}
				Error err = OK;
				Ref<BSParserRef> base_ref = BSCache::get_parser(path, BSParserRef::INHERITANCE_SOLVED, err, parser != nullptr ? parser->script_path : String());
				if (base_ref.is_null() || err != OK || base_ref->get_parser() == nullptr) {
					push_error(vformat(R"(Could not resolve global class base "%s".)", qualified), p_class);
					p_class->base_type = BSParser::DataType();
					return;
				}
				BSParser::ClassNode *base_class = base_ref->get_parser()->get_tree();
				result.kind = BSParser::DataType::CLASS;
				result.class_type = base_class;
				result.script_path = path;
				result.native_type = ScriptServer::get_global_class_native_base(StringName(qualified));
				if (result.native_type == StringName() && base_class != nullptr) {
					result.native_type = base_class->base_type.native_type;
				}
				result.builtin_type = Variant::OBJECT;
				result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
				found = true;
				extends_index = p_class->extends.size(); // Fully consumed by the qualified global name.
			}
		}

		if (!found) {
			// Foundry same-file / scope class lookup @ c9d5e35 (`fs_analyzer_surface.cpp`).
			List<BSParser::ClassNode *> script_classes;
			get_class_node_current_scope_classes(p_class, &script_classes, first_id);
			for (BSParser::ClassNode *look_class : script_classes) {
				if (look_class->identifier != nullptr && look_class->identifier->name == first) {
					if (!look_class->base_type.is_set()) {
						resolve_class_inheritance(look_class);
					}
					// Foundry returns ERR when nested resolve fails; refuse a CLASS edge if the
					// target is still unset / mid-flight RESOLVING (avoids `extends Foo` self-loop).
					if (!look_class->base_type.is_set()) {
						p_class->base_type = BSParser::DataType();
						return;
					}
					result.kind = BSParser::DataType::CLASS;
					result.class_type = look_class;
					result.script_path = parser != nullptr ? parser->script_path : String();
					result.native_type = look_class->base_type.native_type;
					result.builtin_type = Variant::OBJECT;
					result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
					found = true;
					break;
				}
				if (look_class->has_member(first)) {
					const BSParser::ClassNode::Member member = look_class->get_member(first);
					if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
						if (!member.m_class->base_type.is_set()) {
							resolve_class_inheritance(member.m_class);
						}
						if (!member.m_class->base_type.is_set()) {
							p_class->base_type = BSParser::DataType();
							return;
						}
						result.kind = BSParser::DataType::CLASS;
						result.class_type = member.m_class;
						result.script_path = parser != nullptr ? parser->script_path : String();
						result.native_type = member.m_class->base_type.native_type;
						result.builtin_type = Variant::OBJECT;
						result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
						found = true;
						break;
					}
					push_error(vformat(R"(Cannot use %s "%s" in extends chain.)", member.get_type_name(), first), first_id);
					p_class->base_type = BSParser::DataType();
					return;
				}
			}
		}

		if (!found) {
			push_error(vformat(R"(Could not find base class "%s".)", first), first_id);
			p_class->base_type = BSParser::DataType();
			return;
		}

		// Nested extends chain: `extends Outer.Inner` after the first identifier resolved to CLASS.
		for (int index = extends_index; index < p_class->extends.size(); index++) {
			BSParser::IdentifierNode *id = p_class->extends[index];
			if (result.kind != BSParser::DataType::CLASS || result.class_type == nullptr) {
				push_error(vformat(R"(Cannot get nested types for extension from non-BaristaScript type "%s".)", result.to_string()), id);
				p_class->base_type = BSParser::DataType();
				return;
			}
			if (!result.class_type->has_member(id->name)) {
				push_error(vformat(R"(Could not find nested type "%s".)", id->name), id);
				p_class->base_type = BSParser::DataType();
				return;
			}
			const BSParser::ClassNode::Member member = result.class_type->get_member(id->name);
			if (member.type != BSParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
				push_error(vformat(R"(Identifier "%s" is not a preloaded script or class.)", id->name), id);
				p_class->base_type = BSParser::DataType();
				return;
			}
			if (!member.m_class->base_type.is_set()) {
				resolve_class_inheritance(member.m_class);
			}
			if (!member.m_class->base_type.is_set()) {
				p_class->base_type = BSParser::DataType();
				return;
			}
			result.class_type = member.m_class;
			result.native_type = member.m_class->base_type.native_type;
			result.script_path = parser != nullptr ? parser->script_path : String();
		}
	}

	if (result.kind == BSParser::DataType::CLASS && result.class_type != nullptr && result.class_type->is_trait) {
		const String class_name = p_class->identifier != nullptr ? String(p_class->identifier->name) : String("<main>");
		const String trait_name = result.class_type->identifier != nullptr ? String(result.class_type->identifier->name) : String("<trait>");
		const BSParser::Node *source = p_class->extends.is_empty() ? static_cast<const BSParser::Node *>(p_class) : p_class->extends[0];
		push_error(vformat(R"(Class "%s" cannot extend trait "%s"; use "uses %s" instead.)", class_name, trait_name, trait_name), source);
		p_class->base_type = BSParser::DataType();
		return;
	}

	if (result.kind == BSParser::DataType::CLASS && result.class_type != nullptr && result.class_type->is_final) {
		const BSParser::Node *source = p_class->extends.is_empty() ? static_cast<const BSParser::Node *>(p_class) : p_class->extends[0];
		push_error(vformat(R"(Cannot extend final class "%s".)", result.to_string()), source);
		p_class->base_type = BSParser::DataType();
		return;
	}

	// Cyclic inheritance through CLASS bases: Foundry push_errors here. BaristaScript keeps the
	// CLASS edge so mutual path-extends still terminate in `bs_global_class.cpp` (blank editor
	// base) without fail-stopping registration / can_instantiate.

	// M5: extends type arguments are recorded but not specialized here (prior M3 behavior).
	// Fail-stopping would reject well-formed `extends "Generic"[T]` heads that global-class
	// registration still inherits through the unspecialized base native type.
	(void)p_class->extends_type_arguments;

	p_class->base_type = result;
	class_meta.native_type = result.native_type;
	p_class->set_datatype(class_meta);

	// Nested classes after the outer is solved (Foundry recursive inheritance pass).
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::CLASS) {
			resolve_class_inheritance(member.m_class);
		}
	}
}

Error BSAnalyzer::run_phase_inheritance_resolution() {
	BSParser::ClassNode *head = parser->get_tree();
	if (head == nullptr) {
		return ERR_PARSE_ERROR;
	}
	resolve_class_inheritance(head);
	BSParser::DataType self_type;
	self_type.kind = BSParser::DataType::CLASS;
	self_type.class_type = head;
	self_type.script_path = parser->script_path;
	self_type.native_type = head->base_type.native_type;
	self_type.builtin_type = Variant::OBJECT;
	self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	head->set_datatype(self_type);
	mark_phase(AnalyzerPhase::INHERITANCE_RESOLUTION);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

void BSAnalyzer::resolve_datatype(BSParser::DataType &r_type, BSParser::Node *p_source) {
	// Datatype nodes are already partially filled by the parser for builtins. Unknown identifiers
	// in type position become errors when still unresolved after the parser's type-name pass.
	if (r_type.kind == BSParser::DataType::UNRESOLVED) {
		push_error("Could not resolve type.", p_source);
		r_type.kind = BSParser::DataType::VARIANT;
	}
	if (!r_type.type_arguments.is_empty()) {
		// M5: specialization is deferred. Do not erase arguments silently.
		push_error("Generic type specialization is not available until M5.", p_source);
	}
}

BSParser::TypeAliasNode *BSAnalyzer::find_type_alias_in_scope(const StringName &p_name) const {
	for (BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
		if (!scope->has_member(p_name)) {
			continue;
		}
		const BSParser::ClassNode::Member member = scope->get_member(p_name);
		return member.type == BSParser::ClassNode::Member::TYPE_ALIAS ? member.type_alias : nullptr;
	}
	return nullptr;
}

BSParser::DataType BSAnalyzer::resolve_type_alias(BSParser::TypeAliasNode *p_type_alias) {
	BSParser::DataType failed;
	if (p_type_alias == nullptr || p_type_alias->identifier == nullptr || p_type_alias->aliased_type == nullptr) {
		return failed;
	}
	if (const BSParser::DataType *cached = resolved_type_aliases.getptr(p_type_alias)) {
		return *cached;
	}
	if (failed_type_aliases.has(p_type_alias)) {
		return failed;
	}

	int cycle_start = -1;
	for (int i = 0; i < type_alias_resolution_stack.size(); i++) {
		if (type_alias_resolution_stack[i] == p_type_alias) {
			cycle_start = i;
			break;
		}
	}
	if (cycle_start >= 0) {
		BSParser::TypeAliasNode *reported_at = p_type_alias;
		String cycle;
		for (int i = cycle_start; i < type_alias_resolution_stack.size(); i++) {
			BSParser::TypeAliasNode *participant = type_alias_resolution_stack[i];
			cycle += vformat(R"("%s" -> )", participant->identifier->name);
			if (participant->start_line < reported_at->start_line ||
					(participant->start_line == reported_at->start_line && participant->start_column < reported_at->start_column)) {
				reported_at = participant;
			}
		}
		cycle += vformat(R"("%s")", p_type_alias->identifier->name);
		push_error(vformat(R"(Type alias %s expands to itself, so it names no type.)", cycle), reported_at);
		for (int i = cycle_start; i < type_alias_resolution_stack.size(); i++) {
			failed_type_aliases.insert(type_alias_resolution_stack[i]);
		}
		return failed;
	}

	type_alias_resolution_stack.push_back(p_type_alias);
	const int error_count = parser->get_errors().size();
	const BSParser::DataType resolved = datatype_from_type_node(p_type_alias->aliased_type);
	type_alias_resolution_stack.remove_at(type_alias_resolution_stack.size() - 1);
	if (failed_type_aliases.has(p_type_alias)) {
		return failed;
	}
	if (!resolved.is_set() || parser->get_errors().size() > error_count) {
		failed_type_aliases.insert(p_type_alias);
		push_error(vformat(R"(Type alias "%s" has no expansion, because its definition does not name a resolvable type.)",
						   p_type_alias->identifier->name),
				p_type_alias->identifier);
		return failed;
	}
	resolved_type_aliases.insert(p_type_alias, resolved);
	return resolved;
}

BSParser::DataType BSAnalyzer::datatype_from_type_node(BSParser::TypeNode *p_type_node) {
	BSParser::DataType result;
	if (p_type_node == nullptr) {
		result.kind = BSParser::DataType::VARIANT;
		return result;
	}
	result.is_nullable = p_type_node->is_nullable;
	if (p_type_node->is_union) {
		Vector<BSParser::DataType> members;
		for (int i = 0; i < p_type_node->union_member_types.size(); i++) {
			members.push_back(datatype_from_type_node(p_type_node->union_member_types[i]));
		}
		result = BSParser::DataType::make_union(members);
		result.is_nullable = result.is_nullable || p_type_node->is_nullable;
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}
	if (p_type_node->is_tuple) {
		result.kind = BSParser::DataType::TUPLE;
		result.builtin_type = Variant::ARRAY;
		for (int i = 0; i < p_type_node->tuple_element_types.size(); i++) {
			result.container_element_types.push_back(datatype_from_type_node(p_type_node->tuple_element_types[i]));
		}
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}
	if (p_type_node->type_chain.is_empty()) {
		// Foundry datatype_from_type_node @ c9d5e35: `void` parses as an empty type_chain and
		// lowers to BUILTIN/NIL (not VARIANT), including Callable[[...], void] return slots.
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = Variant::NIL;
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}
	// Foundry datatype_from_type_node @ c9d5e35 (~3161): Coroutine[T] is a source-level skin over
	// BSFunctionState rather than a real class / M5 generic. The parser marks TypeNode::is_coroutine
	// for the bracketed form and guarantees a single result type when well-formed; wrong arity still
	// reaches here (empty or over-specified) and must fail-stop rather than fall into M5 generics.
	if (p_type_node->is_coroutine) {
		const StringName head = p_type_node->type_chain[0]->name;
		if (p_type_node->type_chain.size() != 1 || head != SNAME("Coroutine") || p_type_node->container_types.size() != 1) {
			push_error("Coroutine[T] expects exactly one result type argument.", p_type_node);
			result.kind = BSParser::DataType::VARIANT;
			return result;
		}
		BSParser::DataType result_type = datatype_from_type_node(p_type_node->container_types[0]);
		result = make_coroutine_type(result_type);
		result.is_nullable = p_type_node->is_nullable;
		return result;
	}

	if (!p_type_node->container_types.is_empty() || !p_type_node->type_argument_expressions.is_empty()) {
		// Generic / container specialization — deferred unless it is a plain builtin container.
		const StringName head = p_type_node->type_chain[0]->name;
		if (head == SNAME("Array") || head == SNAME("Dictionary")) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = head == SNAME("Array") ? Variant::ARRAY : Variant::DICTIONARY;
			result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			for (int i = 0; i < p_type_node->container_types.size(); i++) {
				result.container_element_types.push_back(datatype_from_type_node(p_type_node->container_types[i]));
			}
			return result;
		}
		push_error("Generic type specialization is not available until M5.", p_type_node);
		result.kind = BSParser::DataType::VARIANT;
		return result;
	}

	StringName name = p_type_node->type_chain[0]->name;
	if (p_type_node->type_chain.size() == 1) {
		if (name == BSParser::get_number_type_name()) {
			// Foundry @ c9d5e35: `Number` is the closed int|float union at builtin precedence.
			result = BSParser::make_number_type();
			result.is_nullable = p_type_node->is_nullable;
			return result;
		}

		// Foundry get_builtin_data_type / datatype_from_type_node @ c9d5e35: every Variant
		// builtin spelling (StringName, Callable, bare Array, NodePath, …) resolves here.
		// Nested builtin enums remain follow-up (godot-cpp lacks Variant::has_enum).
		const bool is_async_callable = name == SNAME("AsyncCallable");
		const Variant::Type builtin_type = is_async_callable ? Variant::CALLABLE : BSParser::get_builtin_type(name);
		if (builtin_type < Variant::VARIANT_MAX || is_async_callable) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = builtin_type;
			result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			result.is_nullable = p_type_node->is_nullable;

			if (builtin_type == Variant::CALLABLE || builtin_type == Variant::SIGNAL) {
				result.signature_is_async = is_async_callable || p_type_node->signature_is_async;
				if (p_type_node->has_signature) {
					result.has_method_signature = true;
					result.has_explicit_method_signature = true;
					MethodInfo method_info;
					for (int i = 0; i < p_type_node->signature_parameter_types.size(); i++) {
						BSParser::DataType parameter_type = datatype_from_type_node(p_type_node->signature_parameter_types[i]);
						parameter_type.is_constant = false;
						result.method_parameter_types.push_back(parameter_type);
						method_info.arguments.push_back(parameter_type.to_property_info(""));
					}
					if (builtin_type == Variant::CALLABLE && p_type_node->signature_rest_parameter_type != nullptr) {
						BSParser::DataType rest_type = datatype_from_type_node(p_type_node->signature_rest_parameter_type);
						if (rest_type.is_set() && rest_type.is_hard_type()) {
							if (rest_type.kind != BSParser::DataType::BUILTIN || rest_type.builtin_type != Variant::ARRAY) {
								push_error(vformat(R"(The Callable rest parameter type must be "Array", but "%s" is specified.)", rest_type.to_string()),
										p_type_node->signature_rest_parameter_type);
							} else {
								method_info.flags |= METHOD_FLAG_VARARG;
								if (BSTypeCompatibility::rest_parameter_type_is_narrowing(rest_type)) {
									rest_type.is_constant = false;
									result.set_method_rest_parameter_type(rest_type);
								}
							}
						}
					}
					if (builtin_type == Variant::CALLABLE) {
						BSParser::DataType return_type;
						if (p_type_node->signature_return_type != nullptr) {
							return_type = datatype_from_type_node(p_type_node->signature_return_type);
						} else {
							return_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
							return_type.kind = BSParser::DataType::BUILTIN;
							return_type.builtin_type = Variant::NIL;
						}
						result.method_return_type.push_back(return_type);
						method_info.return_val = return_type.to_property_info("");
					}
					result.method_info = method_info;
				}
			}
			return result;
		}

		// Legacy lowercase alias kept for source that still spells `string`.
		if (name == SNAME("string")) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = Variant::STRING;
			result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			return result;
		}

		if (name == SNAME("Variant")) {
			result.kind = BSParser::DataType::VARIANT;
			result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			return result;
		}

		if (BSParser::TypeAliasNode *type_alias = find_type_alias_in_scope(name)) {
			// Member resolution installs the declaring scope before expanding/caching the alias.
			// A first use in an inner class must not bind names against that consumer's members.
			for (BSParser::ClassNode *owner = current_class; owner != nullptr; owner = owner->outer) {
				if (owner->has_member(name) && owner->get_member(name).type_alias == type_alias) {
					resolve_class_member(owner, name, p_type_node);
					break;
				}
			}
			if (const BSParser::DataType *resolved = resolved_type_aliases.getptr(type_alias)) {
				result = *resolved;
			}
			if (!result.is_set()) {
				result.kind = BSParser::DataType::VARIANT;
				return result;
			}
			result.is_nullable = result.is_nullable || p_type_node->is_nullable;
			return result;
		}

		// Same-file / enclosing-class named enum before ClassDB / declaration-index lookup.
		{
			BSParser::DataType local_enum = lookup_local_enum_meta_type(name, p_type_node);
			if (local_enum.is_set() && local_enum.kind == BSParser::DataType::ENUM) {
				result = type_from_metatype(local_enum);
				result.is_nullable = p_type_node->is_nullable;
				return result;
			}
		}
		// Same-file class/trait declarations are valid value types in annotations.
		for (BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
			if (!scope->has_member(name)) {
				continue;
			}
			const BSParser::ClassNode::Member member = scope->get_member(name);
			if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
				resolve_class_member(scope, name, p_type_node);
				result = member.m_class->get_datatype();
				result.is_meta_type = false;
				result.is_nullable = p_type_node->is_nullable;
				return result;
			}
			break;
		}
		// Same-file/inherited named tuple declarations resolve to their value type in annotations.
		{
			BSParser::DataType local_tuple;
			if (find_named_tuple_meta_type(name, local_tuple)) {
				result = type_from_metatype(local_tuple);
				result.is_read_only = true;
				result.is_nullable = p_type_node->is_nullable;
				return result;
			}
		}

		if (name == SNAME("Self") && current_class != nullptr) {
			// Foundry datatype_from_type_node @ c9d5e35: Self lowers to @Self bound by the
			// declaring class so trait signature matching can reify it to the implementer.
			if (!p_type_node->container_types.is_empty()) {
				push_error(R"(Type "Self" cannot be specialized with type arguments.)", p_type_node);
				result.kind = BSParser::DataType::VARIANT;
				return result;
			}
			result.kind = BSParser::DataType::TYPE_PARAMETER;
			result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			result.type_parameter_name = SNAME("@Self");
			result.type_parameter_scope = BSParser::DataType::TYPE_PARAMETER_CLASS;
			result.type_parameter_index = -1;
			result.is_nullable = p_type_node->is_nullable;
			BSParser::DataType bound = current_class->get_datatype();
			bound.is_meta_type = false;
			bound.type_arguments.clear();
			if (!bound.is_set() || bound.is_variant()) {
				bound.kind = BSParser::DataType::CLASS;
				bound.class_type = current_class;
				bound.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
				bound.builtin_type = Variant::OBJECT;
				bound.native_type = current_class->base_type.native_type;
			}
			if (bound.is_set() && !bound.is_variant()) {
				result.type_parameter_bound.push_back(bound);
			}
			return result;
		} else if (current_class != nullptr) {
			// Same-file class_name / identifier as a CLASS type (needed for SelfFieldLeg fixtures
			// that annotate parameters as the declaring class rather than `Self`).
			const StringName class_name = current_class->identifier != nullptr ? current_class->identifier->name : StringName();
			const StringName global_name = current_class->get_global_name();
			if ((class_name != StringName() && name == class_name) ||
					(global_name != StringName() && name == global_name)) {
				result = current_class->get_datatype();
				result.is_meta_type = false;
				result.type_arguments.clear();
				if (!result.is_set() || result.is_variant()) {
					result.kind = BSParser::DataType::CLASS;
					result.class_type = current_class;
					result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
					result.builtin_type = Variant::OBJECT;
					result.native_type = current_class->base_type.native_type;
				}
				result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
				result.is_nullable = p_type_node->is_nullable;
				return result;
			}
		}
		if (ClassDB::class_exists(name)) {
			result.kind = BSParser::DataType::NATIVE;
			result.native_type = name;
			result.builtin_type = Variant::OBJECT;
		} else {
			BSParser::DataType indexed = resolve_named_type_in_scope(name, p_type_node);
			if (indexed.kind != BSParser::DataType::VARIANT) {
				return indexed;
			}
			if (ScriptServer::is_global_class(name)) {
				result.kind = BSParser::DataType::CLASS;
				result.script_path = ScriptServer::get_global_class_path(name);
				result.native_type = ScriptServer::get_global_class_native_base(name);
				result.builtin_type = Variant::OBJECT;
			} else {
				push_error(vformat(R"(Could not find type "%s".)", name), p_type_node->type_chain[0]);
				result.kind = BSParser::DataType::VARIANT;
				return result;
			}
		}
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}

	String qualified;
	for (int i = 0; i < p_type_node->type_chain.size(); i++) {
		if (i > 0) {
			qualified += ".";
		}
		qualified += String(p_type_node->type_chain[i]->name);
	}

	// `Message.Move` (or longer chains ending in a case) when the parser asked for an enum case.
	if (p_type_node->type_chain.size() >= 2) {
		const StringName head_name = p_type_node->type_chain[0]->name;
		BSParser::DataType local_enum = lookup_local_enum_meta_type(head_name, p_type_node);
		if (local_enum.is_set() && local_enum.kind == BSParser::DataType::ENUM) {
			if (p_type_node->allows_enum_case && local_enum.is_tagged_union) {
				if (p_type_node->type_chain.size() > 2) {
					push_error(R"(Enum cases cannot contain nested types.)", p_type_node->type_chain[2]);
					result.kind = BSParser::DataType::VARIANT;
					return result;
				}
				const StringName case_name = p_type_node->type_chain[1]->name;
				if (!local_enum.enum_values.has(case_name)) {
					push_error(vformat(R"(Enum "%s" has no case named "%s".)", local_enum.to_string(), case_name), p_type_node->type_chain[1]);
					result.kind = BSParser::DataType::VARIANT;
					return result;
				}
				result = type_from_metatype(local_enum);
				result.enum_case_name = case_name;
				result.is_nullable = p_type_node->is_nullable;
				return result;
			}
			push_error(vformat(R"(Could not find nested type "%s" under base "%s".)", p_type_node->type_chain[1]->name, local_enum.to_string()), p_type_node->type_chain[1]);
			result.kind = BSParser::DataType::VARIANT;
			return result;
		}

		// Foundry datatype_from_type_node / find_named_tuple_meta_type @ c9d5e35:
		// resolve a local owner-qualified named tuple such as `Right.Point` from
		// the same parser tree. Cross-file owner discovery remains #140.
		if (p_type_node->type_chain.size() == 2 && current_class != nullptr) {
			List<BSParser::ClassNode *> scope_classes;
			get_class_node_current_scope_classes(current_class, &scope_classes, p_type_node->type_chain[0]);
			for (BSParser::ClassNode *scope : scope_classes) {
				if (scope == nullptr || !scope->has_member(head_name)) {
					continue;
				}
				const BSParser::ClassNode::Member owner_member = scope->get_member(head_name);
				if (owner_member.type != BSParser::ClassNode::Member::CLASS || owner_member.m_class == nullptr) {
					continue;
				}
				BSParser::ClassNode *owner = owner_member.m_class;
				const StringName tuple_name = p_type_node->type_chain[1]->name;
				if (!owner->has_member(tuple_name)) {
					break;
				}
				const BSParser::ClassNode::Member tuple_member = owner->get_member(tuple_name);
				if (tuple_member.type != BSParser::ClassNode::Member::TUPLE || tuple_member.m_tuple == nullptr) {
					break;
				}
				resolve_class_member(owner, tuple_name, p_type_node->type_chain[1]);
				result = type_from_metatype(tuple_member.m_tuple->get_datatype());
				result.is_read_only = true;
				result.is_nullable = p_type_node->is_nullable;
				return result;
			}
		}
	}

	BSParser::DataType indexed = resolve_named_type(qualified, p_type_node);
	if (indexed.kind != BSParser::DataType::VARIANT) {
		return indexed;
	}
	push_error(vformat(R"(Could not find type "%s".)", qualified), p_type_node->type_chain[0]);
	result.kind = BSParser::DataType::VARIANT;
	return result;
}

void BSAnalyzer::analyze_class_interface(BSParser::ClassNode *p_class, const BSParser::Node *p_source) {
	// Foundry resolve_class_interface @ c9d5e35 (`fs_analyzer_surface.cpp` ~2030): owner INTERFACE
	// failure memoization, foreign SCRIPT raise under ForeignAnalyzerVisibilityScope, and
	// dependent "Could not resolve class" replay with DependentResolutionFailureReplays dedupe.
	if (p_class == nullptr) {
		return;
	}
	ERR_FAIL_NULL(parser);

	const bool owns_class = parser->has_class(p_class) || p_class->is_native_conformance_shim || p_class->is_builtin_conformance_shim;
	if (p_source == nullptr && owns_class) {
		p_source = p_class;
	}

	Ref<BSParserRef> parser_ref;
	if (!owns_class) {
		const String path = _class_script_path_for_foreign_resolve(p_class);
		if (!path.is_empty()) {
			Error err = OK;
			parser_ref = BSCache::get_parser(path, BSParserRef::PARSED, err, parser->script_path);
			if (err != OK) {
				parser_ref = Ref<BSParserRef>();
			}
		}
	}

	const int interface_error_count = parser->get_errors().size();
	Finally record_interface_failure([&]() {
		// M5-only deferred diagnostics (e.g. GENERIC_CLASS heads) must not memoize INTERFACE
		// failure — dependents would replay "Could not resolve class" and break can_instantiate.
		if (owns_class && parser->get_errors().size() > interface_error_count &&
				!errors_from_index_are_only_m5_deferred(interface_error_count)) {
			owner_resolution_failures.record_class(
					p_class, OwnerResolutionFailures::INTERFACE, interface_error_count);
		}
	});
	auto push_external_interface_failure = [&]() {
		if (dependent_resolution_failure_replays.record_class(
					p_class, OwnerResolutionFailures::INTERFACE)) {
			push_error(vformat(R"(Could not resolve class "%s".)", bs_class_or_trait_diagnostic_name(p_class)), p_source);
		}
	};

	if (p_class->resolved_interface) {
		if (!owns_class && parser_ref.is_valid() && parser_ref->get_analyzer() != nullptr &&
				parser_ref->get_analyzer()->owner_resolution_failures.has_class(
						p_class, OwnerResolutionFailures::INTERFACE)) {
			push_external_interface_failure();
		}
		return;
	}

	if (!owns_class) {
		if (parser_ref.is_null() || parser_ref->get_parser() == nullptr) {
			push_error(vformat(R"(Could not resolve class "%s".)", bs_class_or_trait_diagnostic_name(p_class)), p_source);
			return;
		}

		Error err = parser_ref->raise_status(BSParserRef::PARSED);
		if (err != OK) {
			const String path = _class_script_path_for_foreign_resolve(p_class);
			push_error(vformat(R"(Could not parse script "%s" (While resolving class interface).)",
							   bs_diagnostic_file_reference(path.is_empty() ? p_class->get_datatype().script_path : path)),
					p_source);
			return;
		}

		BSAnalyzer *other_analyzer = parser_ref->get_analyzer();
		BSParser *other_parser = parser_ref->get_parser();
		if (other_analyzer == nullptr || other_parser == nullptr) {
			push_error(vformat(R"(Could not resolve class "%s".)", bs_class_or_trait_diagnostic_name(p_class)), p_source);
			return;
		}

		const int error_count = other_parser->get_errors().size();
		ForeignAnalyzerVisibilityScope visibility_scope(other_analyzer);
		other_analyzer->analyze_class_interface(p_class);
		const bool owner_grew_hard_errors = other_parser->get_errors().size() > error_count &&
				!other_analyzer->errors_from_index_are_only_m5_deferred(error_count);
		if (owner_grew_hard_errors ||
				other_analyzer->owner_resolution_failures.has_class(
						p_class, OwnerResolutionFailures::INTERFACE)) {
			push_external_interface_failure();
		}
		return;
	}

	p_class->resolved_interface = true;

	BSParser::ClassNode *previous_class = current_class;
	current_class = p_class;
	for (BSParser::AnnotationNode *annotation : p_class->annotations) {
		if (annotation != nullptr) {
			resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_CLASS);
			annotation->apply(parser, p_class, p_class);
		}
	}

	if (!p_class->base_type.is_resolving()) {
		resolve_class_inheritance(p_class);
	}

	// Foundry: resolve base CLASS interface before members; propagate INTERFACE failures.
	if (p_class->base_type.kind == BSParser::DataType::CLASS && p_class->base_type.class_type != nullptr) {
		BSParser::ClassNode *base_class = p_class->base_type.class_type;
		analyze_class_interface(base_class, p_class);
		if (owner_resolution_failures.has_class(base_class, OwnerResolutionFailures::INTERFACE)) {
			owner_resolution_failures.record_class(p_class, OwnerResolutionFailures::INTERFACE,
					owner_resolution_failures.first_error_index(
							base_class, OwnerResolutionFailures::INTERFACE));
		}
	}

	HashSet<StringName> seen;
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		const StringName name = StringName(member.get_name());
		if (name != StringName()) {
			if (seen.has(name)) {
				push_error(vformat(R"(Member "%s" is declared more than once.)", name), member.get_source_node());
			}
			seen.insert(name);
			// Foundry dispatches conflict policy by member kind
			// (`fs_analyzer_surface.cpp:1755-2015` @ c9d5e35). Methods may override an
			// inherited method or share a native/builtin spelling and only conflict with visible
			// lexical outers. Type aliases have a dedicated type-namespace policy in their member arm.
			if (member.type == BSParser::ClassNode::Member::FUNCTION) {
				check_outer_class_member_name_conflict(p_class, name, member.get_source_node());
			} else if (member.type != BSParser::ClassNode::Member::TYPE_ALIAS) {
				check_class_member_name_conflict(p_class, name, member.get_source_node());
			}
		}
		if (member.type == BSParser::ClassNode::Member::CLASS) {
			// Nested class interface after inheritance (Foundry resolve_class_member CLASS arm
			// only solves inheritance; recurse interface here as before).
			if (member.m_class != nullptr && !member.m_class->base_type.is_resolving()) {
				resolve_class_inheritance(member.m_class);
			}
			analyze_class_interface(member.m_class, p_source);
			continue;
		}
		// Foundry resolve_class_interface @ c9d5e35: each member via resolve_class_member.
		resolve_class_member(p_class, i);
		if (owner_resolution_failures.has_member(p_class, i)) {
			owner_resolution_failures.record_class(p_class, OwnerResolutionFailures::INTERFACE,
					owner_resolution_failures.member_first_error_index(p_class, i));
		}
	}
	if (!p_class->type_parameters.is_empty()) {
		push_error("Generic class specialization is not available until M5.", p_class);
	}
	current_class = previous_class;
}

Error BSAnalyzer::run_phase_interface_and_member_surface() {
	analyze_class_interface(parser->get_tree());
	resolve_used_traits(parser->get_tree());
	mark_phase(AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE);
	// Foundry TRAIT_CONFORMANCE: raise load-graph deps, then registration / extend targets.
	// Abstract-method requirements belong in FLOW_FINALITY_INVARIANTS.
	raise_declared_conformance_dependencies();
	resolve_conformances(parser->get_tree());
	// Imported files raised only to INTERFACE_SOLVED must still publish typed annotation
	// signatures for their consumers (Foundry run_phase_trait_conformance_registration).
	resolve_annotation_declaration_signatures();
	mark_phase(AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

// --- Foundry get_operation_type helpers (@ c9d5e35, fs_analyzer.cpp ~17999+) ---
// Hard fork: FS*→BS*; D1 deletes mixed INT/UINT carrier-widening + numeric_type stamping.

static BSParser::DataType _operation_type_for_operand_pair(Variant::Operator p_operation, const BSParser::DataType &p_a, const BSParser::DataType &p_b, bool &r_valid) {
	if (p_operation == Variant::OP_AND || p_operation == Variant::OP_OR) {
		// Those work for any type of argument and always return a boolean.
		// They don't use the Variant operator since they have short-circuit semantics.
		r_valid = true;
		BSParser::DataType result;
		result.type_source = BSParser::DataType::ANNOTATED_INFERRED;
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = Variant::BOOL;
		return result;
	}

	Variant::Type a_type = p_a.builtin_type;
	Variant::Type b_type = p_b.builtin_type;

	// A tagged-union value is a read-only `[tag, payload...]` Array, so it is never an integer.
	if (p_a.kind == BSParser::DataType::ENUM) {
		if (p_a.is_meta_type) {
			a_type = Variant::DICTIONARY;
		} else {
			a_type = p_a.is_tagged_union ? Variant::ARRAY : Variant::INT;
		}
	}
	if (p_b.kind == BSParser::DataType::ENUM) {
		if (p_b.is_meta_type) {
			b_type = Variant::DICTIONARY;
		} else {
			b_type = p_b.is_tagged_union ? Variant::ARRAY : Variant::INT;
		}
	}

	// The Array erasure is a representation detail, not part of the union's surface: only identity
	// comparison is meaningful on a case value. Concatenation, containment, and the other Array
	// operators would otherwise leak through and silently produce a plain Array.
	if ((p_a.is_tagged_union_type() && !p_a.is_meta_type) || (p_b.is_tagged_union_type() && !p_b.is_meta_type)) {
		if (p_operation != Variant::OP_EQUAL && p_operation != Variant::OP_NOT_EQUAL) {
			r_valid = !(p_a.is_hard_type() && p_b.is_hard_type());
			BSParser::DataType invalid;
			invalid.kind = BSParser::DataType::VARIANT;
			return invalid;
		}
	}

	BSParser::DataType result;
	bool hard_operation = p_a.is_hard_type() && p_b.is_hard_type();

	if (p_operation == Variant::OP_ADD && a_type == Variant::ARRAY && b_type == Variant::ARRAY) {
		if (p_a.has_container_element_type(0) && p_b.has_container_element_type(0)) {
			if (p_a.get_container_element_type(0) == p_b.get_container_element_type(0)) {
				r_valid = true;
				result = p_a;
				result.type_source = hard_operation ? BSParser::DataType::ANNOTATED_INFERRED : BSParser::DataType::INFERRED;
				return result;
			}

			r_valid = false;
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = Variant::ARRAY;
			result.type_source = hard_operation ? BSParser::DataType::ANNOTATED_INFERRED : BSParser::DataType::INFERRED;
			return result;
		}
	}

	// D1: Foundry's mixed INT/UINT carrier-widening arm (FSNumericOps / NumericType) is deleted.
	const bool validated = BSVariantOperators::has_validated_evaluator(p_operation, a_type, b_type);

	if (validated) {
		r_valid = true;
		result.type_source = hard_operation ? BSParser::DataType::ANNOTATED_INFERRED : BSParser::DataType::INFERRED;
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = BSVariantOperators::get_return_type(p_operation, a_type, b_type);
	} else {
		r_valid = !hard_operation;
		result.kind = BSParser::DataType::VARIANT;
	}

	return result;
}

static void _collect_operand_alternatives(const BSParser::DataType &p_type, Vector<BSParser::DataType> &r_alternatives) {
	if (!p_type.is_nullable && !p_type.is_meta_type) {
		if (p_type.kind == BSParser::DataType::UNION) {
			r_alternatives = p_type.union_members;
			return;
		}
		if (p_type.kind == BSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_bound.size() == 1) {
			const BSParser::DataType &bound = p_type.type_parameter_bound[0];
			if (bound.kind == BSParser::DataType::UNION && !bound.is_nullable) {
				r_alternatives = bound.union_members;
				return;
			}
		}
	}
	r_alternatives.push_back(p_type);
}

static bool _operation_is_checked_set_wise(Variant::Operator p_operation) {
	return p_operation != Variant::OP_EQUAL && p_operation != Variant::OP_NOT_EQUAL;
}

static bool _needs_set_wise_operation(Variant::Operator p_operation, const Vector<BSParser::DataType> &p_a_alternatives, const Vector<BSParser::DataType> &p_b_alternatives) {
	return _operation_is_checked_set_wise(p_operation) && (p_a_alternatives.size() > 1 || p_b_alternatives.size() > 1);
}

static bool _find_unsupported_operand_pair(Variant::Operator p_operation, const BSParser::DataType &p_a, const BSParser::DataType &p_b, BSParser::DataType &r_a_alternative, BSParser::DataType &r_b_alternative) {
	Vector<BSParser::DataType> a_alternatives;
	Vector<BSParser::DataType> b_alternatives;
	_collect_operand_alternatives(p_a, a_alternatives);
	_collect_operand_alternatives(p_b, b_alternatives);
	if (!_needs_set_wise_operation(p_operation, a_alternatives, b_alternatives)) {
		return false;
	}

	for (const BSParser::DataType &a_alternative : a_alternatives) {
		for (const BSParser::DataType &b_alternative : b_alternatives) {
			bool pair_valid = false;
			_operation_type_for_operand_pair(p_operation, a_alternative, b_alternative, pair_valid);
			if (!pair_valid) {
				r_a_alternative = a_alternative;
				r_b_alternative = b_alternative;
				return true;
			}
		}
	}
	return false;
}

static String _make_set_operation_error(const BSParser::DataType &p_a, const BSParser::DataType &p_b,
		const BSParser::DataType &p_a_alternative, const BSParser::DataType &p_b_alternative,
		Variant::Operator p_operation, const String &p_pair_error) {
	if (p_pair_error.is_empty()) {
		return vformat(R"(Operands "%s" and "%s" allow the combination "%s" and "%s", which the "%s" operator has no result for. Narrow both operands with a type test, or convert them explicitly.)",
				p_a.to_string_diagnostic(), p_b.to_string_diagnostic(),
				p_a_alternative.to_string_diagnostic(), p_b_alternative.to_string_diagnostic(),
				_operator_name(p_operation));
	}
	return vformat(R"(Operands "%s" and "%s" allow a combination the "%s" operator has no result for. %s Narrow both operands with a type test first.)",
			p_a.to_string_diagnostic(), p_b.to_string_diagnostic(),
			_operator_name(p_operation), p_pair_error);
}

BSParser::DataType BSAnalyzer::get_operation_type(Variant::Operator p_operation, const BSParser::DataType &p_a, bool &r_valid, const BSParser::Node *p_source) {
	// Unary version.
	BSParser::DataType nil_type;
	nil_type.builtin_type = Variant::NIL;
	nil_type.type_source = BSParser::DataType::ANNOTATED_INFERRED;
	return get_operation_type(p_operation, p_a, nil_type, r_valid, p_source);
}

BSParser::DataType BSAnalyzer::get_operation_type(Variant::Operator p_operation, const BSParser::DataType &p_a, const BSParser::DataType &p_b, bool &r_valid, const BSParser::Node *p_source) {
	(void)p_source;
	Vector<BSParser::DataType> a_alternatives;
	Vector<BSParser::DataType> b_alternatives;
	_collect_operand_alternatives(p_a, a_alternatives);
	_collect_operand_alternatives(p_b, b_alternatives);
	if (!_needs_set_wise_operation(p_operation, a_alternatives, b_alternatives)) {
		return _operation_type_for_operand_pair(p_operation, p_a, p_b, r_valid);
	}

	// Set-wise checking: the operation is valid only when every permitted combination has a result,
	// and its type is the normalized union of those results, which collapses to a single type when
	// they all agree. A combination with no result is reported by the caller, which names the pair.
	const bool hard_operation = p_a.is_hard_type() && p_b.is_hard_type();
	Vector<BSParser::DataType> results;
	for (const BSParser::DataType &a_alternative : a_alternatives) {
		for (const BSParser::DataType &b_alternative : b_alternatives) {
			bool pair_valid = false;
			BSParser::DataType pair_result = _operation_type_for_operand_pair(p_operation, a_alternative, b_alternative, pair_valid);
			if (!pair_valid) {
				r_valid = false;
				BSParser::DataType invalid;
				invalid.kind = BSParser::DataType::VARIANT;
				return invalid;
			}
			if (pair_result.is_variant()) {
				// One unconstrained combination makes the whole result unconstrained; a union holding
				// `Variant` would claim more than the operation proves.
				r_valid = true;
				BSParser::DataType dynamic_result;
				dynamic_result.kind = BSParser::DataType::VARIANT;
				return dynamic_result;
			}
			// Members are recorded as written types: `DataType::operator==`, which the normalizer
			// deduplicates with, treats an inferred type as equal to every other type.
			pair_result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			results.push_back(pair_result);
		}
	}

	if (results.is_empty()) {
		r_valid = false;
		BSParser::DataType invalid;
		invalid.kind = BSParser::DataType::VARIANT;
		return invalid;
	}

	r_valid = true;
	BSParser::DataType result = BSParser::DataType::make_union(results);
	result.type_source = hard_operation ? BSParser::DataType::ANNOTATED_INFERRED : BSParser::DataType::INFERRED;
	return result;
}

void BSAnalyzer::reduce_literal(BSParser::LiteralNode *p_literal) {
	if (p_literal == nullptr) {
		return;
	}
	p_literal->is_constant = true;
	p_literal->reduced = true;
	p_literal->reduced_value = p_literal->value;
	p_literal->set_datatype(type_from_variant(p_literal->value));
}

void BSAnalyzer::reduce_unary_op(BSParser::UnaryOpNode *p_unary_op) {
	if (p_unary_op == nullptr || p_unary_op->operand == nullptr) {
		return;
	}
	reduce_expression(p_unary_op->operand);
	BSParser::DataType operand_type = p_unary_op->operand->get_datatype();

	if (p_unary_op->operand->is_constant) {
		p_unary_op->is_constant = true;
		p_unary_op->reduced = true;
		String overflow_error;
		Variant checked;
		if (p_unary_op->variant_op == Variant::OP_NEGATE &&
				_checked_int_binary(Variant::OP_NEGATE, p_unary_op->operand->reduced_value, Variant(), checked, overflow_error)) {
			p_unary_op->reduced_value = checked;
		} else if (p_unary_op->variant_op == Variant::OP_NEGATE && !overflow_error.is_empty()) {
			push_error(overflow_error, p_unary_op);
			p_unary_op->reduced_value = 0;
		} else {
			bool valid = false;
			Variant::evaluate(p_unary_op->variant_op, p_unary_op->operand->reduced_value, Variant(), p_unary_op->reduced_value, valid);
			if (!valid) {
				push_error(vformat(R"(Invalid operand for unary operator "%s".)", _operator_name(p_unary_op->variant_op)), p_unary_op);
				p_unary_op->reduced_value = Variant();
			}
		}
		p_unary_op->set_datatype(type_from_variant(p_unary_op->reduced_value));
		return;
	}

	BSParser::DataType result;
	if (operand_type.is_variant()) {
		result.kind = BSParser::DataType::VARIANT;
		if (strict_dynamic_checks) {
			push_error(vformat(R"*(Cannot use dynamic operand for unary "%s" operator in strict dynamic mode.)*", _operator_name(p_unary_op->variant_op)), p_unary_op);
		} else {
			mark_node_unsafe(p_unary_op);
		}
	} else {
		bool valid = false;
		result = get_operation_type(p_unary_op->variant_op, operand_type, valid, p_unary_op);
		if (!valid) {
			push_error(vformat(R"(Invalid operand of type "%s" for unary operator "%s".)", operand_type.to_string(), _operator_name(p_unary_op->variant_op)), p_unary_op);
		}
	}
	p_unary_op->set_datatype(result);
}

void BSAnalyzer::reduce_binary_op(BSParser::BinaryOpNode *p_binary_op) {
	if (p_binary_op == nullptr) {
		return;
	}
	reduce_expression(p_binary_op->left_operand);
	reduce_expression(p_binary_op->right_operand);
	if (p_binary_op->left_operand == nullptr || p_binary_op->right_operand == nullptr) {
		return;
	}

	BSParser::DataType left_type = p_binary_op->left_operand->get_datatype();
	BSParser::DataType right_type = p_binary_op->right_operand->get_datatype();
	if (!left_type.is_set() || !right_type.is_set()) {
		return;
	}

#ifdef DEBUG_ENABLED
	if (p_binary_op->variant_op == Variant::OP_DIVIDE &&
			(left_type.builtin_type == Variant::INT ||
					left_type.builtin_type == Variant::VECTOR2I ||
					left_type.builtin_type == Variant::VECTOR3I ||
					left_type.builtin_type == Variant::VECTOR4I) &&
			(right_type.builtin_type == Variant::INT ||
					right_type.builtin_type == left_type.builtin_type)) {
		push_warning(p_binary_op, BSWarning::INTEGER_DIVISION);
	}
#endif

	if (p_binary_op->left_operand->is_constant && p_binary_op->right_operand->is_constant) {
		p_binary_op->is_constant = true;
		p_binary_op->reduced = true;
		String overflow_error;
		Variant checked;
		if (_checked_int_binary(p_binary_op->variant_op, p_binary_op->left_operand->reduced_value, p_binary_op->right_operand->reduced_value, checked, overflow_error)) {
			p_binary_op->reduced_value = checked;
		} else if (!overflow_error.is_empty()) {
			push_error(overflow_error, p_binary_op);
			p_binary_op->reduced_value = 0;
		} else {
			bool valid = false;
			Variant::evaluate(p_binary_op->variant_op, p_binary_op->left_operand->reduced_value, p_binary_op->right_operand->reduced_value, p_binary_op->reduced_value, valid);
			if (!valid) {
				push_error(vformat(R"(Invalid operands to operator %s, %s and %s.)",
								   _operator_name(p_binary_op->variant_op),
								   Variant::get_type_name(p_binary_op->left_operand->reduced_value.get_type()),
								   Variant::get_type_name(p_binary_op->right_operand->reduced_value.get_type())),
						p_binary_op);
				p_binary_op->reduced_value = Variant();
			}
		}
		p_binary_op->set_datatype(type_from_variant(p_binary_op->reduced_value));
		return;
	}

	BSParser::DataType result;
	if ((p_binary_op->variant_op == Variant::OP_EQUAL || p_binary_op->variant_op == Variant::OP_NOT_EQUAL) &&
			((left_type.kind == BSParser::DataType::BUILTIN && left_type.builtin_type == Variant::NIL) ||
					(right_type.kind == BSParser::DataType::BUILTIN && right_type.builtin_type == Variant::NIL))) {
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = Variant::BOOL;
	} else if (p_binary_op->variant_op == Variant::OP_MODULE && left_type.builtin_type == Variant::STRING) {
		result.type_source = left_type.type_source;
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = Variant::STRING;
	} else if (left_type.is_variant() || right_type.is_variant()) {
		result.kind = BSParser::DataType::VARIANT;
		if (strict_dynamic_checks) {
			push_error(vformat(R"*(Cannot use dynamic operand for "%s" operator in strict dynamic mode.)*", _operator_name(p_binary_op->variant_op)), p_binary_op);
		} else {
			mark_node_unsafe(p_binary_op);
		}
	} else if (p_binary_op->variant_op < Variant::OP_MAX) {
		bool valid = false;
		result = get_operation_type(p_binary_op->variant_op, left_type, right_type, valid, p_binary_op);
		if (!valid) {
			const BSParser::DataType &union_type = left_type.is_tagged_union_type() && !left_type.is_meta_type ? left_type : right_type;
			BSParser::DataType left_alternative;
			BSParser::DataType right_alternative;
			if (union_type.is_tagged_union_type() && !union_type.is_meta_type) {
				push_error(vformat(R"*(Operator "%s" is not available on tagged union "%s"; its cases carry payloads, so its values are not integers. Match on the case first.)*",
								   _operator_name(p_binary_op->variant_op), union_type.enum_type),
						p_binary_op);
			} else if (_find_unsupported_operand_pair(p_binary_op->variant_op, left_type, right_type, left_alternative, right_alternative)) {
				push_error(_make_set_operation_error(left_type, right_type, left_alternative, right_alternative,
								   p_binary_op->variant_op, String()),
						p_binary_op);
			} else {
				push_error(vformat(R"(Invalid operands "%s" and "%s" for "%s" operator.)", left_type.to_string(), right_type.to_string(), _operator_name(p_binary_op->variant_op)), p_binary_op);
			}
		} else if (!result.is_hard_type()) {
			mark_node_unsafe(p_binary_op);
		}
	} else {
		ERR_PRINT("Parser bug: unknown binary operation.");
	}

	p_binary_op->set_datatype(result);
}

void BSAnalyzer::maybe_capture_identifier_in_lambda(BSParser::IdentifierNode *p_identifier) {
	// Foundry reduce_identifier capture walk @ c9d5e35: only capturable locals/params;
	// members/globals/constants are skipped by the caller before invoking this.
	if (p_identifier == nullptr || current_lambda == nullptr) {
		return;
	}
	switch (p_identifier->source) {
		case BSParser::IdentifierNode::FUNCTION_PARAMETER:
		case BSParser::IdentifierNode::LOCAL_VARIABLE:
		case BSParser::IdentifierNode::LOCAL_ITERATOR:
		case BSParser::IdentifierNode::LOCAL_BIND:
			break;
		default:
			return;
	}

	BSParser::FunctionNode *function_test = current_lambda->function;
	// Capture across nested lambdas until the declaring function; skip same-lambda locals.
	while (function_test != nullptr && function_test != p_identifier->source_function &&
			function_test->source_lambda != nullptr &&
			!function_test->source_lambda->captures_indices.has(p_identifier->name)) {
		function_test->source_lambda->captures_indices[p_identifier->name] = function_test->source_lambda->captures.size();
		function_test->source_lambda->captures.push_back(p_identifier);
		flow_finality.mark_flow_narrowing_capture(p_identifier);
		function_test = function_test->source_lambda->parent_function;
	}
}

void BSAnalyzer::reduce_identifier(BSParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return;
	}
	// Parser-produced pattern/case binds already carry their declaration even when their branch
	// suite is not the identifier's retained suite. Foundry starts from this source tag before
	// attempting broader lookup (`fs_analyzer.cpp:12551-12614` @ c9d5e35).
	if (p_identifier->source == BSParser::IdentifierNode::LOCAL_BIND && p_identifier->bind_source != nullptr) {
		p_identifier->set_datatype(p_identifier->bind_source->get_datatype());
		maybe_capture_identifier_in_lambda(p_identifier);
		return;
	}
	// Suite locals (including parameters) are declared during parse; bind them first so flow
	// finality can see LOCAL_VARIABLE / variable_source for `final var` assignment targets.
	if (p_identifier->suite != nullptr && p_identifier->suite->has_local(p_identifier->name)) {
		const BSParser::SuiteNode::Local &local = p_identifier->suite->get_local(p_identifier->name);
		p_identifier->source_function = local.source_function;
		switch (local.type) {
			case BSParser::SuiteNode::Local::CONSTANT: {
				// Foundry: suite locals/parameters do not re-count usages in reduce_identifier
				// (parse-time binding owns the count). Member/signal paths still bump below.
				p_identifier->source = BSParser::IdentifierNode::LOCAL_CONSTANT;
				p_identifier->constant_source = local.constant;
				if (local.constant != nullptr) {
					p_identifier->set_datatype(local.constant->get_datatype());
					if (local.constant->initializer != nullptr && local.constant->initializer->is_constant) {
						p_identifier->is_constant = true;
						p_identifier->reduced_value = local.constant->initializer->reduced_value;
					}
				}
				return;
			}
			case BSParser::SuiteNode::Local::VARIABLE: {
				p_identifier->source = BSParser::IdentifierNode::LOCAL_VARIABLE;
				p_identifier->variable_source = local.variable;
				if (local.variable != nullptr) {
					p_identifier->set_datatype(local.variable->get_datatype());
				}
				if (const BSParser::DataType *narrowed = flow_finality.lookup_flow_narrowed_type(flow_finality.flow_narrowing_key_from_identifier(p_identifier))) {
					p_identifier->set_datatype(*narrowed);
				}
				maybe_capture_identifier_in_lambda(p_identifier);
				return;
			}
			case BSParser::SuiteNode::Local::PARAMETER: {
				p_identifier->source = BSParser::IdentifierNode::FUNCTION_PARAMETER;
				p_identifier->parameter_source = local.parameter;
				if (local.parameter != nullptr) {
					p_identifier->set_datatype(local.parameter->get_datatype());
				}
				if (const BSParser::DataType *narrowed = flow_finality.lookup_flow_narrowed_type(flow_finality.flow_narrowing_key_from_identifier(p_identifier))) {
					p_identifier->set_datatype(*narrowed);
				}
				maybe_capture_identifier_in_lambda(p_identifier);
				return;
			}
			case BSParser::SuiteNode::Local::FOR_VARIABLE:
			case BSParser::SuiteNode::Local::PATTERN_BIND:
			case BSParser::SuiteNode::Local::CASE_BIND: {
				p_identifier->source = local.type == BSParser::SuiteNode::Local::FOR_VARIABLE ? BSParser::IdentifierNode::LOCAL_ITERATOR : BSParser::IdentifierNode::LOCAL_BIND;
				p_identifier->bind_source = local.bind;
				if (local.bind != nullptr) {
					p_identifier->set_datatype(local.bind->get_datatype());
				}
				if (const BSParser::DataType *narrowed = flow_finality.lookup_flow_narrowed_type(flow_finality.flow_narrowing_key_from_identifier(p_identifier))) {
					p_identifier->set_datatype(*narrowed);
				}
				maybe_capture_identifier_in_lambda(p_identifier);
				return;
			}
			default:
				break;
		}
	}
	if (current_function != nullptr) {
		for (int i = 0; i < current_function->parameters.size(); i++) {
			BSParser::ParameterNode *parameter = current_function->parameters[i];
			if (parameter != nullptr && parameter->identifier != nullptr && parameter->identifier->name == p_identifier->name) {
				p_identifier->source = BSParser::IdentifierNode::FUNCTION_PARAMETER;
				p_identifier->parameter_source = parameter;
				p_identifier->set_datatype(parameter->get_datatype());
				p_identifier->source_function = current_function;
				if (const BSParser::DataType *narrowed = flow_finality.lookup_flow_narrowed_type(flow_finality.flow_narrowing_key_from_identifier(p_identifier))) {
					p_identifier->set_datatype(*narrowed);
				}
				maybe_capture_identifier_in_lambda(p_identifier);
				return;
			}
		}
	}
	for (BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
		if (try_bind_identifier_member_in_inheritance(p_identifier, scope, scope != current_class)) {
			return;
		}
	}
	// Foundry surface: flattened trait members are visible on the implementer (#60).
	if (current_class != nullptr) {
		for (int t = 0; t < current_class->resolved_traits.size(); t++) {
			BSParser::ClassNode *trait = current_class->resolved_traits[t];
			if (try_bind_identifier_member(p_identifier, trait, false)) {
				return;
			}
		}
	}
	// Own class_name / identifier as a CLASS meta handle (`Receiver.Message.…` EXACT_HANDLE).
	if (current_class != nullptr) {
		const StringName class_name = current_class->identifier != nullptr ? current_class->identifier->name : StringName();
		const StringName global_name = current_class->get_global_name();
		if ((class_name != StringName() && p_identifier->name == class_name) ||
				(global_name != StringName() && p_identifier->name == global_name)) {
			BSParser::DataType class_meta = current_class->get_datatype();
			if (!class_meta.is_set() || class_meta.is_variant()) {
				class_meta.kind = BSParser::DataType::CLASS;
				class_meta.class_type = current_class;
				class_meta.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
				class_meta.builtin_type = Variant::OBJECT;
				class_meta.native_type = current_class->base_type.native_type;
			}
			class_meta.is_meta_type = true;
			p_identifier->set_datatype(class_meta);
			return;
		}
	}
	// Foundry reduce_identifier Self class-handle @ c9d5e35: expression-position `Self` is the
	// receiver-relative `@Self` meta handle (needed for `Self.Message.…` SelfFieldLeg selection).
	if (p_identifier->name == SNAME("Self") && current_class != nullptr && current_function != nullptr) {
		BSParser::DataType self_handle;
		self_handle.kind = BSParser::DataType::TYPE_PARAMETER;
		self_handle.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		self_handle.type_parameter_name = SNAME("@Self");
		self_handle.type_parameter_scope = BSParser::DataType::TYPE_PARAMETER_CLASS;
		self_handle.type_parameter_index = -1;
		self_handle.is_meta_type = true;
		self_handle.is_type_handle_annotation = true;
		BSParser::DataType bound = current_class->get_datatype();
		bound.is_meta_type = false;
		bound.type_arguments.clear();
		if (!bound.is_set() || bound.is_variant()) {
			bound.kind = BSParser::DataType::CLASS;
			bound.class_type = current_class;
			bound.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			bound.builtin_type = Variant::OBJECT;
			bound.native_type = current_class->base_type.native_type;
		}
		if (bound.is_set() && !bound.is_variant()) {
			self_handle.type_parameter_bound.push_back(bound);
		}
		p_identifier->source = BSParser::IdentifierNode::STATIC_SELF_CLASS;
		p_identifier->set_datatype(self_handle);
		return;
	}
	// Native classes are expression-position class handles after locals and members have missed.
	// This also lets match patterns distinguish an unshadowed native type from a value pattern.
	if (ClassDB::class_exists(p_identifier->name)) {
		BSParser::DataType native_meta;
		native_meta.kind = BSParser::DataType::NATIVE;
		native_meta.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		native_meta.builtin_type = Variant::OBJECT;
		native_meta.native_type = p_identifier->name;
		native_meta.is_meta_type = true;
		native_meta.is_constant = true;
		p_identifier->source = BSParser::IdentifierNode::NATIVE_CLASS;
		p_identifier->set_datatype(native_meta);
		return;
	}
	BSParser::DataType indexed = resolve_named_type_in_scope(p_identifier->name, p_identifier);
	if (!indexed.is_variant()) {
		indexed.is_meta_type = true;
		p_identifier->set_datatype(indexed);
		return;
	}
	if (CoreConstants::is_global_constant(p_identifier->name)) {
		const int index = CoreConstants::get_global_constant_index(p_identifier->name);
		p_identifier->reduced_value = CoreConstants::get_global_constant_value(index);
		p_identifier->is_constant = true;
		p_identifier->set_datatype(type_from_variant(p_identifier->reduced_value));
		return;
	}
	MethodInfo utility;
	const bool language_utility = BSUtilityFunctions::get_function_info(p_identifier->name, utility);
	if (language_utility || CoreConstants::get_utility_function(p_identifier->name, utility)) {
		p_identifier->set_datatype(call_site_validation.explicit_callable_type_from_info(utility));
		if (language_utility) {
			p_identifier->is_constant = true;
			p_identifier->reduced_value = BSUtilityFunctions::make_analyzer_callable(p_identifier->name);
		}
		return;
	}
	if (find_type_alias_in_scope(p_identifier->name) != nullptr) {
		push_error(vformat(R"(Type alias "%s" can only be used in a type position. It declares no value, so it cannot be called, constructed, or read.)",
						   p_identifier->name),
				p_identifier);
		BSParser::DataType alias_use;
		alias_use.kind = BSParser::DataType::VARIANT;
		p_identifier->set_datatype(alias_use);
		return;
	}
	// Foundry reduce_identifier @ c9d5e35 (`fs_analyzer.cpp:12875-12879`): once every
	// local/member/type/global lookup has missed, report at the identifier token and retain a
	// Variant datatype only to suppress dependent type cascades.
	push_error(vformat(R"(Identifier "%s" not declared in the current scope.)", p_identifier->name), p_identifier);
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_identifier->set_datatype(type);
}

// Forward decls for Self-contract helpers defined later in this TU (Foundry @ c9d5e35).
namespace {
bool _is_self_type_parameter(const BSParser::DataType &p_type);
BSParser::DataType _self_type_for_class(BSParser::ClassNode *p_class);
BSParser::DataType _self_type_parameter_from_bound(const BSParser::DataType &p_bound);
BSParser::DataType _self_type_parameter_for_class(BSParser::ClassNode *p_class);
bool _datatype_contains_self_type_parameter(const BSParser::DataType &p_type);
BSParser::DataType _substitute_self_type_parameter(const BSParser::DataType &p_type, const BSParser::DataType &p_self_type);
BSParser::DataType _substitute_self_type_parameter_with_bounds(const BSParser::DataType &p_type, bool p_mark_substitution);
bool _datatype_matches_analyzer_substituted_self(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_actual_type);
bool _self_contract_admits_value_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_value_type, BSAnalyzer::SelfContractKind p_kind, const BSParser::ExpressionNode *p_value_source, BSParser::DataType *r_matched_value);
} // namespace

void BSAnalyzer::validate_local_call(BSParser::CallNode *p_call, BSParser::FunctionNode *p_callee) {
	if (p_call == nullptr || p_callee == nullptr) {
		return;
	}
	BSParser::ClassNode *declaring_class = current_class;
	const auto find_owner = [&](const auto &self, BSParser::ClassNode *candidate) -> BSParser::ClassNode * {
		if (candidate == nullptr) {
			return nullptr;
		}
		for (const BSParser::ClassNode::Member &member : candidate->members) {
			if (member.type == BSParser::ClassNode::Member::FUNCTION && member.function == p_callee) {
				return candidate;
			}
			if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
				if (BSParser::ClassNode *owner = self(self, member.m_class)) {
					return owner;
				}
			}
		}
		return nullptr;
	};
	if (parser != nullptr) {
		if (BSParser::ClassNode *owner = find_owner(find_owner, parser->head)) {
			declaring_class = owner;
		}
	}
	// Foundry get_function_signature @ c9d5e35: bare async script/local calls type as
	// Coroutine[T] (NATIVE BSFunctionState skin), not bare T — enables MISSING_AWAIT and
	// honest Coroutine[T] assignment for `async_fn()` / `obj.async_method()`.
	auto set_local_call_return_type = [&]() {
		BSParser::DataType return_type = p_callee->get_datatype();
		return_type.is_meta_type = false;
		if (p_callee->is_coroutine) {
			return_type = make_coroutine_type(return_type);
		}
		p_call->set_datatype(return_type);
	};
	// Named arguments rewrite into canonical positional order before arity/type checks
	// (Foundry CallSiteValidationContext::canonicalize_named_call_arguments @ c9d5e35).
	if (!call_site_validation.canonicalize_named_call_arguments(p_call, p_callee)) {
		set_local_call_return_type();
		return;
	}
	// Foundry get_function_signature @ c9d5e35: parameter-position Self is an exact receiver
	// contract for ordinary instance calls. Stamp before validate_call_arg so PARAMETER admission
	// and identity gates see the provenance; returns keep the frame Self without the stamp.
	const bool parameter_self_is_receiver_contract = !p_callee->is_static;
	BSParser::DataType parameter_self_type;
	if (parameter_self_is_receiver_contract && declaring_class != nullptr) {
		parameter_self_type = _self_type_parameter_from_bound(_self_type_for_class(declaring_class));
		parameter_self_type.is_receiver_self_contract = true;
	}

	List<BSParser::DataType> par_types;
	int default_arg_count = 0;
	for (int i = 0; i < p_callee->parameters.size(); i++) {
		BSParser::ParameterNode *parameter = p_callee->parameters[i];
		if (parameter == nullptr) {
			par_types.push_back(BSParser::DataType());
			continue;
		}
		BSParser::DataType par_type = parameter->get_datatype();
		if (_datatype_contains_self_type_parameter(par_type)) {
			if (parameter_self_is_receiver_contract && parameter_self_type.is_set()) {
				par_type = _substitute_self_type_parameter(par_type, parameter_self_type);
			} else if (!parameter_self_is_receiver_contract && declaring_class != nullptr) {
				par_type = _substitute_self_type_parameter(par_type, _self_type_for_class(declaring_class));
			}
		}
		par_types.push_back(par_type);
	}
	// Defaults must be trailing; count only the trailing run so arity matches Foundry.
	for (int i = p_callee->parameters.size() - 1; i >= 0; i--) {
		if (p_callee->parameters[i] != nullptr && p_callee->parameters[i]->initializer != nullptr) {
			default_arg_count++;
		} else {
			break;
		}
	}
	p_call->resolved_parameter_types.clear();
	for (const BSParser::DataType &par_type : par_types) {
		p_call->resolved_parameter_types.push_back(par_type);
	}
	const BSParser::DataType *rest_type = nullptr;
	BSParser::DataType rest_storage;
	if (p_callee->is_vararg() && p_callee->rest_parameter != nullptr) {
		rest_storage = p_callee->rest_parameter->get_datatype();
		if (parameter_self_is_receiver_contract && parameter_self_type.is_set() &&
				_datatype_contains_self_type_parameter(rest_storage)) {
			rest_storage = _substitute_self_type_parameter(rest_storage, parameter_self_type);
		}
		rest_type = &rest_storage;
	}
	call_site_validation.validate_call_arg(par_types, default_arg_count, p_callee->is_vararg(), p_call, Vector<int>(), 0, rest_type);
	set_local_call_return_type();
}

void BSAnalyzer::reduce_call(BSParser::CallNode *p_call, bool p_is_await, bool p_is_root) {
	if (p_call == nullptr) {
		return;
	}
	for (int i = 0; i < p_call->arguments.size(); i++) {
		reduce_expression(p_call->arguments[i]);
	}

	// Foundry @ c9d5e35: any call may observe / mutate captured locals, so drop narrowing
	// that was only proven before a lambda captured those locals.
	Finally clear_captured_flow_narrowing_after_call([&]() {
		flow_finality.clear_captured_flow_narrowing();
	});

	// Foundry MISSING_AWAIT @ c9d5e35 (~9178): fire on every exit once the call's datatype is set.
	// Honest Coroutine[T] typing makes a held or passed coroutine well-typed, so only a discarded
	// coroutine *statement* (root position) still warns about a probably-forgotten "await".
	// Coroutine[void] root discards are exempt: fire-and-forget launches lose no result value.
#ifdef DEBUG_ENABLED
	Finally warn_missing_await([&]() {
		const BSParser::DataType call_type = p_call->get_datatype();
		if (call_type.is_coroutine && !p_is_await && p_is_root && !coroutine_result_is_void(call_type)) {
			Vector<String> symbols;
			symbols.push_back(call_type.to_string());
			push_warning(p_call, BSWarning::MISSING_AWAIT, symbols);
		}
	});
#else
	(void)p_is_await;
	(void)p_is_root;
#endif

	// Attribute call: `receiver.method(...)` — signal.emit and member-method shapes.
	if (p_call->get_callee_type() == BSParser::Node::SUBSCRIPT) {
		BSParser::SubscriptNode *subscript = static_cast<BSParser::SubscriptNode *>(p_call->callee);
		if (subscript != nullptr && subscript->base == nullptr) {
			if (subscript->is_contextual_enum_case) {
				// Foundry @ c9d5e35: contextual `.Case(...)` waits for the consumer's expected type.
				if (p_call->function_name == StringName() && subscript->attribute != nullptr) {
					p_call->function_name = subscript->attribute->name;
				}
				register_contextual_enum_case(p_call);
				BSParser::DataType call_type;
				call_type.kind = BSParser::DataType::VARIANT;
				p_call->set_datatype(call_type);
				return;
			}
			BSParser::DataType call_type;
			call_type.kind = BSParser::DataType::VARIANT;
			p_call->set_datatype(call_type);
			return;
		}
		if (subscript != nullptr && subscript->is_attribute && subscript->attribute != nullptr) {
			reduce_expression(subscript->base);
			const bool is_self = subscript->base != nullptr && subscript->base->type == BSParser::Node::SELF;
			if (p_call->function_name == StringName()) {
				p_call->function_name = subscript->attribute->name;
			}

			// Foundry reduce_call tuple-construction path @ c9d5e35: an owner-qualified
			// local constructor (`Left.Point(...)`) resolves against the precise class
			// declaration before ordinary method lookup.
			if (subscript->base != nullptr && p_call->function_name != StringName()) {
				const BSParser::DataType owner_type = subscript->base->get_datatype();
				BSParser::DataType tuple_meta_type;
				if (find_named_tuple_meta_type(owner_type, is_self, p_call->function_name, subscript->attribute, tuple_meta_type)) {
					p_call->receiver_is_current_self = is_self;
					reduce_call_tuple_construction(p_call, tuple_meta_type);
					return;
				}
			}

			// Foundry reduce_call @ c9d5e35: payload-carrying tagged-union case construction
			// (`Message.Attach(...)`, `self.Message.Attach(...)`, …).
			if (subscript->base != nullptr && p_call->function_name != StringName()) {
				const BSParser::DataType enum_base_type = subscript->base->get_datatype();
				if (enum_base_type.is_set() && enum_base_type.kind == BSParser::DataType::ENUM && enum_base_type.is_meta_type &&
						enum_base_type.is_tagged_union && enum_base_type.get_enum_case_payload(p_call->function_name) != nullptr) {
					reduce_call_enum_case_construction(p_call, enum_base_type);
					return;
				}
			}

			// A local class metatype's new() produces that precise class value, so subsequent
			// method calls retain their local signatures instead of degrading to native Object.
			if (subscript->base != nullptr && p_call->function_name == SNAME("new")) {
				const BSParser::DataType class_meta_type = subscript->base->get_datatype();
				if (class_meta_type.kind == BSParser::DataType::CLASS && class_meta_type.is_meta_type) {
					BSParser::FunctionNode *initializer = find_class_function(class_meta_type.class_type, SNAME("_init"));
					if (initializer != nullptr) {
						validate_local_call(p_call, initializer);
					} else {
						call_site_validation.reject_named_call_arguments(p_call);
						if (!p_call->arguments.is_empty()) {
							push_error(vformat(R"*(Too many arguments for "new()" call. Expected at most 0 but received %d.)*", p_call->arguments.size()), p_call->arguments[0]);
						}
					}
					BSParser::DataType value_type = class_meta_type;
					value_type.is_meta_type = false;
					value_type.is_constant = false;
					p_call->set_datatype(value_type);
					return;
				}
			}

			if (subscript->base != nullptr && p_call->function_name == SNAME("emit")) {
				const BSParser::DataType base_type = subscript->base->get_datatype();
				if (base_type.kind == BSParser::DataType::BUILTIN && base_type.builtin_type == Variant::SIGNAL &&
						base_type.has_method_signature) {
					call_site_validation.validate_signal_emit_args(base_type, p_call, 0);
					BSParser::DataType void_type;
					void_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
					void_type.kind = BSParser::DataType::BUILTIN;
					void_type.builtin_type = Variant::NIL;
					p_call->set_datatype(void_type);
					return;
				}
			}

			// Signal-value connect/disconnect/is_connected: `registered.connect(handler)`.
			if (subscript->base != nullptr &&
					(p_call->function_name == SNAME("connect") || p_call->function_name == SNAME("disconnect") ||
							p_call->function_name == SNAME("is_connected"))) {
				const BSParser::DataType base_type = subscript->base->get_datatype();
				if (base_type.kind == BSParser::DataType::BUILTIN && base_type.builtin_type == Variant::SIGNAL &&
						base_type.has_method_signature) {
					call_site_validation.reject_named_call_arguments(p_call);
					call_site_validation.validate_signal_connect_arg(base_type, p_call, 0);
					BSParser::DataType void_type;
					void_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
					void_type.kind = BSParser::DataType::BUILTIN;
					void_type.builtin_type = Variant::NIL;
					if (p_call->function_name == SNAME("is_connected")) {
						void_type.builtin_type = Variant::BOOL;
					}
					p_call->set_datatype(void_type);
					return;
				}
			}

			// Foundry @ c9d5e35: typed Callable.bind / bindv / unbind / call signature transforms.
			if (subscript->base != nullptr && p_call->function_name != StringName()) {
				const BSParser::DataType base_type = subscript->base->get_datatype();
				if (call_site_validation.try_type_callable_method_call(p_call, base_type)) {
					return;
				}
			}

			// Native MethodInfo path on a typed native / class receiver (Foundry validate_call_arg(MethodInfo)).
			if (subscript->base != nullptr && p_call->function_name != StringName()) {
				const BSParser::DataType base_type = subscript->base->get_datatype();
				StringName native_type;
				if (base_type.kind == BSParser::DataType::NATIVE) {
					native_type = base_type.native_type;
				} else if (base_type.kind == BSParser::DataType::CLASS && base_type.native_type != StringName()) {
					native_type = base_type.native_type;
				} else if (base_type.kind == BSParser::DataType::TYPE_PARAMETER &&
						base_type.type_parameter_name == SNAME("@Self") && !base_type.type_parameter_bound.is_empty()) {
					native_type = base_type.type_parameter_bound[0].native_type;
				} else if (is_self && current_class != nullptr && current_class->base_type.native_type != StringName()) {
					native_type = current_class->base_type.native_type;
				}
				// CLASS inheritance member methods before native fallback (Foundry @ c9d5e35).
				// `@Self`-typed receivers (expression `self`, Self-typed locals) resolve against the bound class.
				BSParser::ClassNode *method_owner = nullptr;
				if (base_type.kind == BSParser::DataType::CLASS && base_type.class_type != nullptr) {
					method_owner = base_type.class_type;
				} else if (base_type.kind == BSParser::DataType::TYPE_PARAMETER &&
						base_type.type_parameter_name == SNAME("@Self") && !base_type.type_parameter_bound.is_empty() &&
						base_type.type_parameter_bound[0].kind == BSParser::DataType::CLASS) {
					method_owner = base_type.type_parameter_bound[0].class_type;
				}
				if (method_owner != nullptr) {
					BSParser::FunctionNode *callee = find_class_function(method_owner, p_call->function_name);
					if (callee != nullptr) {
						validate_local_call(p_call, callee);
						p_call->is_noreturn = callee->is_noreturn;
						return;
					}
				} else if (is_self && current_class != nullptr) {
					BSParser::FunctionNode *callee = find_class_function(current_class, p_call->function_name);
					if (callee != nullptr) {
						validate_local_call(p_call, callee);
						p_call->is_noreturn = callee->is_noreturn;
						return;
					}
				}
				if (native_type != StringName()) {
					MethodInfo method_info;
					if (BSNativeDB::get_method_info(native_type, p_call->function_name, &method_info)) {
						call_site_validation.reject_named_call_arguments(p_call);
						call_site_validation.validate_call_arg(method_info, p_call);
						call_site_validation.validate_typed_object_signal_api_args(base_type, p_call, is_self);
						// Foundry @ c9d5e35: after MethodInfo on self.emit_signal / connect, still run typed
						// payload / callable checks against the named local signal.
						if (is_self && p_call->function_name == SNAME("emit_signal")) {
							call_site_validation.validate_local_object_emit_signal_args(p_call, true);
						}
						if (is_self) {
							call_site_validation.validate_local_object_signal_callable_arg(p_call, true);
						}
						mark_implicit_signal_usage(p_call, is_self);
						p_call->set_datatype(type_from_property(method_info.return_val));
						return;
					}
				}
				// Foundry get_function_signature apply_conformance_witness @ c9d5e35: after the
				// target's own CLASS / native surface misses, fall back to a retroactive witness.
				// Instance witnesses need a receiver (reject on meta-type / class handles).
				{
					BSParser::DataType witness_base = base_type;
					// Expression `self` is `@Self`; witness lookup keys on the bound CLASS/NATIVE.
					if (witness_base.kind == BSParser::DataType::TYPE_PARAMETER &&
							witness_base.type_parameter_name == SNAME("@Self") &&
							!witness_base.type_parameter_bound.is_empty()) {
						witness_base = witness_base.type_parameter_bound[0];
						witness_base.is_meta_type = false;
					} else if (is_self && current_class != nullptr &&
							(witness_base.kind == BSParser::DataType::UNRESOLVED ||
									witness_base.kind == BSParser::DataType::VARIANT ||
									!witness_base.is_set())) {
						witness_base = current_class->get_datatype();
					}
					BSParser::FunctionNode *witness = find_conformance_witness(witness_base, p_call->function_name);
					if (witness != nullptr && (witness->is_static || !witness_base.is_meta_type)) {
						validate_local_call(p_call, witness);
						p_call->is_noreturn = witness->is_noreturn;
						return;
					}
					// Foundry unresolved-call path @ c9d5e35: a hidden (not visible) witness is
					// rejected with a concrete diagnostic instead of a silent member-miss.
					if (!p_call->is_super) {
						String hidden_conformance_source;
						StringName hidden_conformance_trait;
						if (find_hidden_conformance_witness(witness_base, p_call->function_name,
									hidden_conformance_source, hidden_conformance_trait)) {
							push_error(vformat(R"*(Cannot call "%s()" on "%s": it is supplied by the retroactive conformance to trait "%s" declared in "%s", which this file does not load. Import that file's namespace, or preload it.)*",
											   p_call->function_name, witness_base.to_string(), hidden_conformance_trait,
											   bs_diagnostic_file_reference(hidden_conformance_source)),
									p_call->callee != nullptr ? p_call->callee : static_cast<const BSParser::Node *>(p_call));
							BSParser::DataType call_type;
							call_type.kind = BSParser::DataType::VARIANT;
							p_call->set_datatype(call_type);
							return;
						}
					}
				}
			}
		}
	}

	// A bare named tuple declaration is a constructor before ordinary function/native lookup.
	if (p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER) {
		const StringName tuple_name = static_cast<BSParser::IdentifierNode *>(p_call->callee)->name;
		BSParser::DataType tuple_meta_type;
		BSParser::DataType receiver_type = current_class != nullptr ? current_class->get_datatype() : BSParser::DataType();
		receiver_type.is_meta_type = false;
		if (find_named_tuple_meta_type(receiver_type, true, tuple_name, p_call, tuple_meta_type)) {
			p_call->function_name = tuple_name;
			p_call->receiver_is_current_self = true;
			reduce_call_tuple_construction(p_call, tuple_meta_type);
			return;
		}
	}

	// Foundry builtin constructor specialization @ c9d5e35: validate the pinned engine overloads,
	// then preserve the target signature carried by Callable(Object, method) and Signal(Object, signal).
	StringName constructor_name = p_call->function_name;
	if (constructor_name == StringName() && p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER) {
		constructor_name = static_cast<BSParser::IdentifierNode *>(p_call->callee)->name;
	}
	const bool callable_constructor = constructor_name == SNAME("Callable");
	const bool signal_constructor = constructor_name == SNAME("Signal");
	if ((callable_constructor || signal_constructor) &&
			(p_call->callee == nullptr || p_call->callee->type == BSParser::Node::IDENTIFIER)) {
		call_site_validation.reject_named_call_arguments(p_call);
		const Variant::Type builtin_type = callable_constructor ? Variant::CALLABLE : Variant::SIGNAL;
		BSParser::DataType constructor_type = type_from_property(PropertyInfo(builtin_type, ""));
		// The pinned engine producer declares exactly (), (same carrier), and
		// (Object, StringName) overloads for both types
		// (`godot-cpp/gdextension/extension_api-4-7.json:19959-19984,20130-20155`).
		// Validate that overload surface before adding Foundry's richer target signature.
		auto argument_matches = [&](int p_index, const BSParser::DataType &p_expected) {
			if (p_index < 0 || p_index >= p_call->arguments.size() || p_call->arguments[p_index] == nullptr) {
				return false;
			}
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = true;
			options.strict_dynamic = strict_dynamic_checks;
			options.strict_null = strict_null_checks;
			if (p_call->arguments[p_index]->is_constant) {
				options.constant_source_value = &p_call->arguments[p_index]->reduced_value;
			}
			return BSTypeCompatibility::check(p_expected, p_call->arguments[p_index]->get_datatype(), options).compatible;
		};
		bool valid_constructor = p_call->arguments.is_empty();
		if (p_call->arguments.size() == 1) {
			const BSParser::DataType source_type = p_call->arguments[0]->get_datatype();
			// Copy construction is carrier identity, not general type coercion. A genuinely gradual
			// value remains a runtime check in non-strict mode; a concrete Object/@Self does not.
			valid_constructor = (source_type.kind == BSParser::DataType::BUILTIN &&
										source_type.builtin_type == builtin_type) ||
					(source_type.is_variant() && !strict_dynamic_checks);
		} else if (p_call->arguments.size() == 2) {
			const BSParser::DataType object_type = type_from_property(PropertyInfo(
																			  Variant::OBJECT, "", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT, SNAME("Object")),
					true);
			const BSParser::DataType name_type = type_from_property(PropertyInfo(Variant::STRING_NAME, ""), true);
			const BSParser::DataType receiver_type = p_call->arguments[0] != nullptr
					? p_call->arguments[0]->get_datatype()
					: BSParser::DataType();
			const bool local_object_receiver = receiver_type.kind == BSParser::DataType::CLASS && receiver_type.class_type != nullptr;
			valid_constructor = (local_object_receiver || argument_matches(0, object_type)) && argument_matches(1, name_type);
		}
		if (!valid_constructor) {
			String signature = Variant::get_type_name(builtin_type) + "(";
			for (int i = 0; i < p_call->arguments.size(); i++) {
				if (i > 0) {
					signature += ", ";
				}
				signature += p_call->arguments[i] != nullptr ? p_call->arguments[i]->get_datatype().to_string() : String("Variant");
			}
			signature += ")";
			push_error(vformat(R"(No constructor of "%s" matches the signature "%s".)",
							   Variant::get_type_name(builtin_type), signature),
					p_call);
			p_call->set_datatype(constructor_type);
			return;
		}
		if (p_call->arguments.size() == 2) {
			BSParser::DataType explicit_type;
			const bool found = callable_constructor ? call_site_validation.callable_type_from_constant_method_args(p_call, 0, 1, explicit_type) : call_site_validation.signal_type_from_receiver(p_call->arguments[0]->get_datatype(), p_call, 1, explicit_type);
			if (found) {
				constructor_type = explicit_type;
			} else if (callable_constructor) {
				call_site_validation.validate_strict_callable_method_fallback(p_call, p_call->arguments[0]->get_datatype(), 1);
			} else {
				call_site_validation.validate_strict_signal_name_fallback(p_call, p_call->arguments[0]->get_datatype(), 1);
			}
		}
		// Foundry fs_analyzer.cpp:8265-8351 @ c9d5e35: only empty carriers and constant
		// carrier-preserving copies are safe to fold. Receiver/name construction stays dynamic.
		if (p_call->arguments.is_empty()) {
			p_call->is_constant = true;
			p_call->reduced_value = callable_constructor ? Variant(Callable()) : Variant(Signal());
		} else if (p_call->arguments.size() == 1 && p_call->arguments[0]->is_constant &&
				p_call->arguments[0]->reduced_value.get_type() == builtin_type) {
			p_call->is_constant = true;
			p_call->reduced_value = p_call->arguments[0]->reduced_value;
		}
		p_call->set_datatype(constructor_type);
		return;
	}

	if (current_class != nullptr) {
		StringName fname = p_call->function_name;
		if (fname == StringName() && p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER) {
			fname = static_cast<BSParser::IdentifierNode *>(p_call->callee)->name;
		}
		// Same-class bare call: callee is the identifier itself (or null for some super forms).
		const bool local_shape = p_call->callee == nullptr || p_call->callee->type == BSParser::Node::IDENTIFIER;
		const BSParser::IdentifierNode *identifier = local_shape && p_call->callee != nullptr ? static_cast<BSParser::IdentifierNode *>(p_call->callee) : nullptr;
		const bool has_lexical_callee = identifier != nullptr && identifier->suite != nullptr && identifier->suite->has_local(fname);
		if (local_shape && fname != StringName() && !has_lexical_callee) {
			if (fname == SNAME("emit_signal")) {
				call_site_validation.reject_named_call_arguments(p_call);
				call_site_validation.validate_local_object_emit_signal_args(p_call, true);
				mark_implicit_signal_usage(p_call, true);
				BSParser::DataType void_type;
				void_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
				void_type.kind = BSParser::DataType::BUILTIN;
				void_type.builtin_type = Variant::NIL;
				p_call->set_datatype(void_type);
				return;
			}
			BSParser::FunctionNode *callee = find_class_function(current_class, fname);
			if (callee != nullptr) {
				validate_local_call(p_call, callee);
				p_call->is_noreturn = callee->is_noreturn;
				return;
			}
			// Bare native MethodInfo call on the script's native base (e.g. Node.get_node).
			if (current_class->base_type.native_type != StringName()) {
				MethodInfo method_info;
				if (BSNativeDB::get_method_info(current_class->base_type.native_type, fname, &method_info)) {
					call_site_validation.reject_named_call_arguments(p_call);
					call_site_validation.validate_call_arg(method_info, p_call);
					call_site_validation.validate_local_object_signal_callable_arg(p_call, true);
					// Foundry treats bare identifier callees as self for unused-signal accounting.
					mark_implicit_signal_usage(p_call, true);
					p_call->set_datatype(type_from_property(method_info.return_val));
					return;
				}
			}
			// Foundry apply_conformance_witness on self after local + native miss.
			{
				const BSParser::DataType self_type = current_class->get_datatype();
				BSParser::FunctionNode *witness = find_conformance_witness(self_type, fname);
				if (witness != nullptr && (witness->is_static || !self_type.is_meta_type)) {
					validate_local_call(p_call, witness);
					p_call->is_noreturn = witness->is_noreturn;
					return;
				}
				if (!p_call->is_super) {
					String hidden_conformance_source;
					StringName hidden_conformance_trait;
					if (find_hidden_conformance_witness(self_type, fname, hidden_conformance_source,
								hidden_conformance_trait)) {
						push_error(vformat(R"*(Cannot call "%s()" on "%s": it is supplied by the retroactive conformance to trait "%s" declared in "%s", which this file does not load. Import that file's namespace, or preload it.)*",
										   fname, self_type.to_string(), hidden_conformance_trait,
										   bs_diagnostic_file_reference(hidden_conformance_source)),
								p_call->callee != nullptr ? p_call->callee : static_cast<const BSParser::Node *>(p_call));
						BSParser::DataType call_type;
						call_type.kind = BSParser::DataType::VARIANT;
						p_call->set_datatype(call_type);
						return;
					}
				}
			}
		}
	}
	// Foundry marks `push_fatal` as noreturn at call sites (fs_analyzer.cpp @ c9d5e35).
	if (p_call->function_name == SNAME("push_fatal") ||
			(p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER &&
					static_cast<BSParser::IdentifierNode *>(p_call->callee)->name == SNAME("push_fatal"))) {
		p_call->is_noreturn = true;
	}
	// Every unmatched bare call must pass through the same name lookup as a value read.
	// Builtin constructors and the language's fatal intrinsic are already recognized callees.
	if (p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER &&
			BSParser::get_builtin_type(constructor_name) == Variant::VARIANT_MAX && !p_call->is_noreturn) {
		reduce_identifier(static_cast<BSParser::IdentifierNode *>(p_call->callee));
		const BSParser::DataType callee_type = p_call->callee->get_datatype();
		if (callee_type.kind == BSParser::DataType::BUILTIN && callee_type.builtin_type == Variant::CALLABLE) {
			if (callee_type.has_method_signature) {
				const int previous_errors = parser->get_errors().size();
				call_site_validation.reject_named_call_arguments(p_call);
				call_site_validation.validate_call_arg(callee_type.method_info, p_call);
				p_call->set_datatype(type_from_property(callee_type.method_info.return_val));
				// The existing name pipeline owns precedence. Only a genuine unshadowed language
				// utility reaches this branch with no local/member source (Foundry 8489-8542).
				MethodInfo language_info;
				if (static_cast<BSParser::IdentifierNode *>(p_call->callee)->source == BSParser::IdentifierNode::UNDEFINED_SOURCE &&
						BSUtilityFunctions::get_function_info(constructor_name, language_info)) {
					if (!p_is_root && !p_is_await && language_info.return_val.type == Variant::NIL &&
							!(language_info.return_val.usage & PROPERTY_USAGE_NIL_IS_VARIANT)) {
						push_error(vformat(R"*(Cannot get return value of call to "%s()" because it returns "void".)*", constructor_name), p_call);
					}
					bool all_constant = true;
					Vector<Variant> arguments;
					for (const BSParser::ExpressionNode *argument : p_call->arguments) {
						all_constant = all_constant && argument->is_constant;
						arguments.push_back(argument->reduced_value);
					}
					if (all_constant && parser->get_errors().size() == previous_errors) {
						Variant value;
						String error;
						switch (BSUtilityFunctions::evaluate_constant(constructor_name, arguments, value, error)) {
							case BSUtilityFunctions::ConstantResult::FOLDED:
								p_call->is_constant = true;
								p_call->reduced_value = value;
								break;
							case BSUtilityFunctions::ConstantResult::INVALID_ARGUMENT:
								push_error(vformat(R"*(Invalid argument for "%s()" function: %s)*", constructor_name, error), p_call);
								break;
							case BSUtilityFunctions::ConstantResult::NOT_CONSTANT:
								break;
						}
					}
				}
				return;
			}
		} else if (callee_type.is_hard_type() && !callee_type.is_variant()) {
			push_error(vformat(R"(Cannot call "%s": it is not a function.)", constructor_name), p_call->callee);
		}
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_call->set_datatype(type);
}

void BSAnalyzer::reduce_await(BSParser::AwaitNode *p_await) {
	// Foundry reduce_await @ c9d5e35 (~8014): unwrap Coroutine[T]→T (single-level), signal→Variant,
	// constant non-coroutine passthrough, nullable coroutine nullability propagation.
	if (p_await == nullptr) {
		return;
	}
	if (p_await->to_await == nullptr) {
		BSParser::DataType await_type;
		await_type.kind = BSParser::DataType::VARIANT;
		p_await->set_datatype(await_type);
		return;
	}

	if (p_await->to_await->type == BSParser::Node::CALL) {
		reduce_call(static_cast<BSParser::CallNode *>(p_await->to_await), true);
	} else {
		reduce_expression(p_await->to_await);
	}

	BSParser::DataType operand_type = p_await->to_await->get_datatype();
	BSParser::DataType await_type = operand_type;
	if (operand_type.is_coroutine) {
		// Awaiting a Coroutine[T] yields T from container_element_types[0]; a coroutine without a
		// recorded result type (e.g. a bare AsyncCallable.call()) unwraps to Variant. This is a
		// single-level unwrap: await Coroutine[Coroutine[U]] yields Coroutine[U].
		if (operand_type.has_container_element_type(0)) {
			await_type = operand_type.get_container_element_type(0);
			if (operand_type.is_nullable && !await_type.is_variant() &&
					!(await_type.kind == BSParser::DataType::BUILTIN && await_type.builtin_type == Variant::NIL)) {
				// Awaiting a nullable coroutine can observe a null handle (`await null` yields null at
				// runtime), so the awaited result is nullable too.
				await_type.is_nullable = true;
			}
		} else {
			await_type = BSParser::DataType();
			await_type.kind = BSParser::DataType::VARIANT;
		}
	} else if (operand_type.is_hard_type() && operand_type.kind == BSParser::DataType::BUILTIN && operand_type.builtin_type == Variant::SIGNAL) {
		// We cannot infer the type of the result of waiting for a signal.
		await_type.kind = BSParser::DataType::VARIANT;
		await_type.type_source = BSParser::DataType::UNDETECTED;
	} else if (p_await->to_await->is_constant) {
		p_await->is_constant = p_await->to_await->is_constant;
		p_await->reduced_value = p_await->to_await->reduced_value;
		// Awaiting a plain value yields it unchanged; a non-coroutine operand never carries the flag.
		await_type.is_coroutine = false;
	}
	// The coroutine branch keeps the unwrapped result's own flag, so awaiting Coroutine[Coroutine[U]]
	// correctly stays a Coroutine[U]; only the non-coroutine branches above can leave a stale flag.
	p_await->set_datatype(await_type);

#ifdef DEBUG_ENABLED
	BSParser::DataType to_await_type = p_await->to_await->get_datatype();
	if (!to_await_type.is_coroutine && !to_await_type.is_variant() && to_await_type.builtin_type != Variant::SIGNAL) {
		push_warning(p_await, BSWarning::REDUNDANT_AWAIT);
	}
#endif
}

void BSAnalyzer::reduce_lambda(BSParser::LambdaNode *p_lambda) {
	// Foundry reduce_lambda @ c9d5e35: Callable type + signature now; body after the statement
	// via resolve_pending_lambda_bodies so capture marking runs under the outer suite's
	// flow-narrowing scope without nesting body analysis inside the initializer reduce.
	if (p_lambda == nullptr) {
		return;
	}
	BSParser::DataType lambda_type;
	lambda_type.type_source = BSParser::DataType::ANNOTATED_INFERRED;
	lambda_type.kind = BSParser::DataType::BUILTIN;
	lambda_type.builtin_type = Variant::CALLABLE;
	p_lambda->set_datatype(lambda_type);

	if (p_lambda->function == nullptr) {
		return;
	}

	BSParser::LambdaNode *previous_lambda = current_lambda;
	current_lambda = p_lambda;
	resolve_function_signature_in_class(p_lambda->function, current_class);
	current_lambda = previous_lambda;

	pending_lambda_bodies.push_back(p_lambda);
}

void BSAnalyzer::resolve_pending_lambda_bodies() {
	if (pending_lambda_bodies.is_empty()) {
		return;
	}

	BSParser::LambdaNode *previous_lambda = current_lambda;
	Vector<BSParser::LambdaNode *> lambdas = pending_lambda_bodies;
	pending_lambda_bodies.clear();

	for (int i = 0; i < lambdas.size(); i++) {
		BSParser::LambdaNode *lambda = lambdas[i];
		if (lambda == nullptr || lambda->function == nullptr) {
			continue;
		}
		current_lambda = lambda;
		analyze_function_body(lambda->function, true);
	}

	current_lambda = previous_lambda;
}

void BSAnalyzer::reduce_subscript(BSParser::SubscriptNode *p_subscript) {
	if (p_subscript == nullptr) {
		return;
	}
	if (p_subscript->base == nullptr) {
		if (p_subscript->is_contextual_enum_case) {
			// Foundry @ c9d5e35: payload-less `.Quit` waits for the consumer's expected type.
			register_contextual_enum_case(p_subscript);
		}
		return;
	}
	reduce_expression(p_subscript->base);
	const BSParser::DataType tuple_base_type = p_subscript->base->get_datatype();
	if (p_subscript->is_tuple_index) {
		reduce_expression(p_subscript->index);
		BSParser::DataType result_type;
		result_type.kind = BSParser::DataType::VARIANT;
		if (tuple_base_type.kind == BSParser::DataType::TUPLE && tuple_base_type.is_meta_type) {
			push_error(vformat(R"*(Cannot index the tuple type "%s"; construct a value first.)*", tuple_base_type.to_string()), p_subscript);
		} else if (tuple_base_type.kind == BSParser::DataType::TUPLE) {
			const BSParser::DataType index_type = p_subscript->index != nullptr
					? p_subscript->index->get_datatype()
					: BSParser::DataType();
			const bool has_constant_index = p_subscript->index != nullptr && p_subscript->index->is_constant &&
					p_subscript->index->reduced_value.get_type() == Variant::INT;
			if (!has_constant_index && index_type.is_hard_type() && !index_type.is_variant() &&
					!(index_type.kind == BSParser::DataType::BUILTIN && index_type.builtin_type == Variant::INT)) {
				push_error(vformat(R"*(Only an integer can index tuple "%s", but received "%s".)*",
								   tuple_base_type.to_string(), index_type.to_string()),
						p_subscript->index);
			} else if (!has_constant_index) {
				mark_node_unsafe(p_subscript);
			} else {
				const int64_t index = p_subscript->index->reduced_value;
				if (index < 0 || index >= tuple_base_type.container_element_types.size()) {
					push_error(vformat(R"*(Tuple index %d is out of range for "%s", which has %d element(s).)*",
									   index, tuple_base_type.to_string(), tuple_base_type.container_element_types.size()),
							p_subscript->index);
				} else {
					result_type = tuple_base_type.get_container_element_type(index);
					result_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
					result_type.is_read_only = true;
				}
			}
		} else if (tuple_base_type.is_variant() || !tuple_base_type.is_hard_type()) {
			if (strict_dynamic_checks) {
				push_error("Cannot use tuple index access on Variant in strict dynamic mode.", p_subscript->base);
			} else {
				mark_node_unsafe(p_subscript);
			}
		} else {
			push_error(vformat(R"*(Cannot use tuple index access on a value of type "%s".)*", tuple_base_type.to_string()), p_subscript);
		}
		p_subscript->set_datatype(result_type);
		return;
	}
	if (p_subscript->is_attribute && tuple_base_type.kind == BSParser::DataType::TUPLE && p_subscript->attribute != nullptr) {
		BSParser::DataType result_type;
		result_type.kind = BSParser::DataType::VARIANT;
		if (tuple_base_type.is_meta_type) {
			push_error(vformat(R"*(Cannot access member "%s" on the tuple type "%s"; construct a value first.)*",
							   p_subscript->attribute->name, tuple_base_type.to_string()),
					p_subscript->attribute);
		} else {
			const int field_index = tuple_base_type.get_tuple_field_index(p_subscript->attribute->name);
			if (field_index < 0) {
				push_error(vformat(R"*(Tuple "%s" has no field named "%s".)*",
								   tuple_base_type.to_string(), p_subscript->attribute->name),
						p_subscript->attribute);
			} else {
				result_type = tuple_base_type.get_container_element_type(field_index);
				result_type.is_read_only = true;
				p_subscript->attribute->set_datatype(result_type);
			}
		}
		p_subscript->set_datatype(result_type);
		return;
	}
	if (p_subscript->is_attribute) {
		// Foundry reduce_subscript ENUM meta case fold @ c9d5e35: `Message.Quit` / plain
		// `Level.Low` are constant values (tagged-union `[tag]` singleton or INT ordinal).
		if (p_subscript->attribute != nullptr && p_subscript->base != nullptr) {
			const BSParser::DataType base_type = p_subscript->base->get_datatype();
			const StringName case_name = p_subscript->attribute->name;
			if (base_type.kind == BSParser::DataType::ENUM && base_type.is_meta_type && base_type.enum_values.has(case_name)) {
				BSParser::DataType case_type = type_from_metatype(base_type);
				if (base_type.is_tagged_union) {
					if (base_type.get_enum_case_payload(case_name) != nullptr) {
						case_type.is_pseudo_type = true;
						case_type.enum_case_name = case_name;
						p_subscript->attribute->set_datatype(case_type);
						p_subscript->set_datatype(case_type);
						push_error(vformat(R"*(Enum case "%s.%s" carries a payload and must be constructed, e.g. "%s.%s(...)".)*",
										   base_type.enum_type, case_name, base_type.enum_type, case_name),
								p_subscript);
						return;
					}
					p_subscript->attribute->set_datatype(case_type);
					p_subscript->set_datatype(case_type);
					p_subscript->is_constant = true;
					p_subscript->reduced_value = _tagged_union_case_singleton(base_type.enum_values[case_name]);
					return;
				}
				p_subscript->attribute->set_datatype(case_type);
				p_subscript->set_datatype(case_type);
				p_subscript->is_constant = true;
				p_subscript->reduced_value = base_type.enum_values[case_name];
				return;
			}
		}
		// Bind `self.<member>` and same-class `ClassName.<static>` so flow finality can see
		// MEMBER_VARIABLE / STATIC_VARIABLE / INHERITED_VARIABLE on the attribute
		// (Foundry resolve_subscript @ c9d5e35), including CLASS inheritance chain members.
		if (p_subscript->attribute != nullptr && p_subscript->base != nullptr && current_class != nullptr) {
			const bool self_receiver = p_subscript->base->type == BSParser::Node::SELF;
			bool class_name_receiver = false;
			if (p_subscript->base->type == BSParser::Node::IDENTIFIER) {
				const BSParser::IdentifierNode *base_id = static_cast<const BSParser::IdentifierNode *>(p_subscript->base);
				const StringName class_name = current_class->identifier != nullptr ? current_class->identifier->name : StringName();
				const StringName global_name = current_class->get_global_name();
				class_name_receiver = base_id->name == class_name || (global_name != StringName() && base_id->name == global_name);
			}
			if (self_receiver || class_name_receiver) {
				bool first = true;
				HashSet<const BSParser::ClassNode *> visited;
				for (BSParser::ClassNode *lookup = current_class; lookup != nullptr; lookup = lookup->base_type.class_type) {
					if (visited.has(lookup)) {
						break;
					}
					visited.insert(lookup);
					if (!lookup->has_member(p_subscript->attribute->name)) {
						first = false;
						continue;
					}
					resolve_class_member(lookup, p_subscript->attribute->name, p_subscript->attribute);
					const BSParser::ClassNode::Member member = lookup->get_member(p_subscript->attribute->name);
					if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
						if (class_name_receiver && !member.variable->is_static) {
							// ClassName.instance_member is not a legal static access; leave unbound.
						} else {
							if (!first && !member.variable->is_static) {
								p_subscript->attribute->source = BSParser::IdentifierNode::INHERITED_VARIABLE;
							} else {
								p_subscript->attribute->source = member.variable->is_static ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE;
							}
							p_subscript->attribute->variable_source = member.variable;
							member.variable->usages++;
							p_subscript->attribute->set_datatype(member.variable->get_datatype());
							p_subscript->set_datatype(member.variable->get_datatype());
							return;
						}
					}
					if (member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr) {
						p_subscript->attribute->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
						p_subscript->attribute->constant_source = member.constant;
						member.constant->usages++;
						p_subscript->attribute->set_datatype(member.constant->get_datatype());
						p_subscript->set_datatype(member.constant->get_datatype());
						if (member.constant->initializer != nullptr && member.constant->initializer->is_constant) {
							p_subscript->is_constant = true;
							p_subscript->reduced_value = member.constant->initializer->reduced_value;
						}
						return;
					}
					if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr && !class_name_receiver) {
						p_subscript->attribute->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
						p_subscript->attribute->signal_source = member.signal;
						member.signal->usages++;
						const BSParser::DataType signal_type = call_site_validation.explicit_signal_type_from_node(member.signal, current_class->get_datatype(), lookup);
						p_subscript->attribute->set_datatype(signal_type);
						p_subscript->set_datatype(signal_type);
						return;
					}
					if (member.type == BSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
						BSParser::DataType enum_meta = member.m_enum->get_datatype();
						if (!enum_meta.is_set() && !enum_meta.is_resolving()) {
							const String script_path = parser != nullptr ? parser->script_path : String();
							enum_meta = resolve_enum_values(member.m_enum, make_class_enum_type(p_subscript->attribute->name, lookup, script_path, true), lookup);
						}
						if (enum_meta.is_set() || enum_meta.is_resolving()) {
							p_subscript->attribute->set_datatype(enum_meta);
							p_subscript->set_datatype(enum_meta);
							p_subscript->is_constant = true;
							return;
						}
					}
					// Ordinary declaration claims the name even when this access form cannot use it.
					break;
				}
			}
		}
		// Resolve concrete local-class member values on an arbitrary typed receiver. The earlier
		// self/class-name path records frame-specific finality; this path supplies the same destination
		// evidence for typed instances and inherited members, including readonly constants.
		if (p_subscript->attribute != nullptr && p_subscript->base != nullptr) {
			BSParser::DataType receiver_type = p_subscript->base->get_datatype();
			if (_is_self_type_parameter(receiver_type) && !receiver_type.type_parameter_bound.is_empty()) {
				receiver_type = receiver_type.type_parameter_bound[0];
				receiver_type.is_meta_type = p_subscript->base->get_datatype().is_meta_type;
			}
			if (receiver_type.kind == BSParser::DataType::CLASS && receiver_type.class_type != nullptr) {
				bool inherited = false;
				HashSet<const BSParser::ClassNode *> visited;
				for (BSParser::ClassNode *lookup = receiver_type.class_type; lookup != nullptr; lookup = lookup->base_type.class_type) {
					if (visited.has(lookup)) {
						break;
					}
					visited.insert(lookup);
					if (!lookup->has_member(p_subscript->attribute->name)) {
						inherited = true;
						continue;
					}
					resolve_class_member(lookup, p_subscript->attribute->name, p_subscript->attribute);
					const BSParser::ClassNode::Member member = lookup->get_member(p_subscript->attribute->name);
					if (member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr) {
						p_subscript->attribute->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
						p_subscript->attribute->constant_source = member.constant;
						member.constant->usages++;
						p_subscript->attribute->set_datatype(member.constant->get_datatype());
						p_subscript->set_datatype(member.constant->get_datatype());
						if (member.constant->initializer != nullptr && member.constant->initializer->is_constant) {
							p_subscript->is_constant = true;
							p_subscript->reduced_value = member.constant->initializer->reduced_value;
						}
						return;
					}
					if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr &&
							(!receiver_type.is_meta_type || member.variable->is_static)) {
						p_subscript->attribute->source = member.variable->is_static
								? BSParser::IdentifierNode::STATIC_VARIABLE
								: (inherited ? BSParser::IdentifierNode::INHERITED_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE);
						p_subscript->attribute->variable_source = member.variable;
						member.variable->usages++;
						p_subscript->attribute->set_datatype(member.variable->get_datatype());
						p_subscript->set_datatype(member.variable->get_datatype());
						return;
					}
					if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr && !receiver_type.is_meta_type) {
						p_subscript->attribute->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
						p_subscript->attribute->signal_source = member.signal;
						member.signal->usages++;
						const BSParser::DataType signal_type = call_site_validation.explicit_signal_type_from_node(member.signal, receiver_type, lookup);
						p_subscript->attribute->set_datatype(signal_type);
						p_subscript->set_datatype(signal_type);
						return;
					}
					break;
				}
			}
		}
		// Instance / class-handle / Self-handle `.Enum` for SelfFieldLeg spellings
		// (`receiver.Message`, `Self.Message`) when the receiver is not the frame's own class_name.
		if (p_subscript->attribute != nullptr && p_subscript->base != nullptr) {
			BSParser::DataType base_type = p_subscript->base->get_datatype();
			BSParser::ClassNode *owner = nullptr;
			if (base_type.kind == BSParser::DataType::CLASS && base_type.class_type != nullptr) {
				owner = base_type.class_type;
			} else if (base_type.kind == BSParser::DataType::TYPE_PARAMETER &&
					base_type.type_parameter_name == SNAME("@Self") && !base_type.type_parameter_bound.is_empty() &&
					base_type.type_parameter_bound[0].kind == BSParser::DataType::CLASS) {
				owner = base_type.type_parameter_bound[0].class_type;
			}
			if (owner != nullptr && owner->has_member(p_subscript->attribute->name)) {
				resolve_class_member(owner, p_subscript->attribute->name, p_subscript->attribute);
				const BSParser::ClassNode::Member member = owner->get_member(p_subscript->attribute->name);
				if (member.type == BSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
					BSParser::DataType enum_meta = member.m_enum->get_datatype();
					if (!enum_meta.is_set() && !enum_meta.is_resolving()) {
						const String script_path = parser != nullptr ? parser->script_path : String();
						enum_meta = resolve_enum_values(member.m_enum, make_class_enum_type(p_subscript->attribute->name, owner, script_path, true), owner);
					}
					if (enum_meta.is_set() || enum_meta.is_resolving()) {
						p_subscript->attribute->set_datatype(enum_meta);
						p_subscript->set_datatype(enum_meta);
						p_subscript->is_constant = true;
						return;
					}
				}
			}
		}
	} else {
		reduce_expression(p_subscript->index);
		const BSParser::DataType base_type = p_subscript->base->get_datatype();
		const BSParser::DataType index_type = p_subscript->index != nullptr
				? p_subscript->index->get_datatype()
				: BSParser::DataType();
		BSParser::DataType result_type;
		result_type.kind = BSParser::DataType::VARIANT;
		if (base_type.kind == BSParser::DataType::TUPLE) {
			if (base_type.is_meta_type) {
				push_error(vformat(R"*(Cannot index the tuple type "%s"; construct a value first.)*", base_type.to_string()), p_subscript);
			} else {
				const bool has_constant_index = p_subscript->index != nullptr && p_subscript->index->is_constant &&
						p_subscript->index->reduced_value.get_type() == Variant::INT;
				if (!has_constant_index && index_type.is_hard_type() && !index_type.is_variant() &&
						!(index_type.kind == BSParser::DataType::BUILTIN && index_type.builtin_type == Variant::INT)) {
					push_error(vformat(R"*(Only an integer can index tuple "%s", but received "%s".)*",
									   base_type.to_string(), index_type.to_string()),
							p_subscript->index);
				} else if (!has_constant_index) {
					mark_node_unsafe(p_subscript);
				} else {
					const int64_t index = p_subscript->index->reduced_value;
					if (index < 0 || index >= base_type.container_element_types.size()) {
						push_error(vformat(R"*(Tuple index %d is out of range for "%s", which has %d element(s).)*",
										   index, base_type.to_string(), base_type.container_element_types.size()),
								p_subscript->index);
					} else {
						result_type = base_type.get_container_element_type(index);
						result_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
						result_type.is_read_only = true;
					}
				}
			}
			p_subscript->set_datatype(result_type);
			return;
		}
		if (base_type.kind == BSParser::DataType::BUILTIN &&
				(base_type.builtin_type == Variant::ARRAY || base_type.builtin_type == Variant::DICTIONARY)) {
			BSParser::DataType expected_index_type;
			if (base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_type(0)) {
				expected_index_type = base_type.get_container_element_type(0);
			} else {
				expected_index_type.kind = BSParser::DataType::VARIANT;
			}
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = false;
			options.strict_dynamic = strict_dynamic_checks;
			options.strict_null = strict_null_checks;
			const bool array_index_is_number = base_type.builtin_type == Variant::ARRAY &&
					index_type.kind == BSParser::DataType::BUILTIN &&
					(index_type.builtin_type == Variant::INT || index_type.builtin_type == Variant::FLOAT);
			const bool invalid_concrete_index = base_type.builtin_type == Variant::ARRAY
					? !array_index_is_number
					: !BSTypeCompatibility::check(expected_index_type, index_type, options).compatible;
			if (p_subscript->index != nullptr && index_type.is_set() && !index_type.is_variant() &&
					invalid_concrete_index) {
				push_error(vformat(R"*(Invalid index type "%s" for a base of type "%s".)*",
								   index_type.to_string(), base_type.to_string()),
						p_subscript->index);
			} else if (index_type.is_variant()) {
				if (strict_dynamic_checks) {
					push_error(vformat(R"*(Cannot use dynamic index of type "%s" for base of type "%s" in strict dynamic mode.)*",
									   index_type.to_string(), base_type.to_string()),
							p_subscript->index);
				} else {
					mark_node_unsafe(p_subscript);
				}
			}
			if (base_type.builtin_type == Variant::ARRAY && base_type.has_container_element_type(0)) {
				result_type = base_type.get_container_element_type(0);
			} else if (base_type.builtin_type == Variant::DICTIONARY && base_type.has_container_element_type(1)) {
				result_type = base_type.get_container_element_type(1);
			}
			p_subscript->set_datatype(result_type);
			return;
		}
		if (base_type.is_variant()) {
			if (strict_dynamic_checks) {
				push_error("Cannot use subscript operator on Variant in strict dynamic mode.", p_subscript->base);
			} else {
				mark_node_unsafe(p_subscript);
			}
			p_subscript->set_datatype(result_type);
			return;
		}
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_subscript->set_datatype(type);
}

void BSAnalyzer::reduce_array(BSParser::ArrayNode *p_array) {
	if (p_array == nullptr) {
		return;
	}
	bool all_constant = true;
	Array values;
	for (int i = 0; i < p_array->elements.size(); i++) {
		reduce_expression(p_array->elements[i]);
		if (p_array->elements[i] == nullptr || !p_array->elements[i]->is_constant) {
			all_constant = false;
		} else {
			values.push_back(p_array->elements[i]->reduced_value);
		}
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = Variant::ARRAY;
	if (all_constant) {
		p_array->is_constant = true;
		p_array->reduced = true;
		p_array->reduced_value = values;
		type.is_constant = true;
	}
	p_array->set_datatype(type);
}

void BSAnalyzer::reduce_tuple_literal(BSParser::TupleLiteralNode *p_tuple) {
	if (p_tuple == nullptr) {
		return;
	}
	bool all_constant = true;
	Array values;
	Vector<BSParser::DataType> element_types;
	for (BSParser::ExpressionNode *element : p_tuple->elements) {
		reduce_expression(element);
		BSParser::DataType element_type;
		if (element == nullptr || !element->get_datatype().is_set() || element->get_datatype().is_variant()) {
			element_type.kind = BSParser::DataType::VARIANT;
		} else {
			element_type = element->get_datatype();
			element_type.is_constant = false;
			element_type.is_meta_type = false;
		}
		element_types.push_back(element_type);
		if (element == nullptr || !element->is_constant) {
			all_constant = false;
		} else {
			values.push_back(element->reduced_value);
		}
	}
	BSParser::DataType tuple_type = make_tuple_type(StringName(), String(), String(), element_types, Vector<StringName>(), false);
	if (all_constant) {
		values.make_read_only();
		p_tuple->is_constant = true;
		p_tuple->reduced_value = values;
		tuple_type.is_constant = true;
	}
	p_tuple->set_datatype(tuple_type);
}

void BSAnalyzer::reduce_dictionary(BSParser::DictionaryNode *p_dictionary) {
	if (p_dictionary == nullptr) {
		return;
	}
	// Foundry `reduce_dictionary` @ c9d5e35 (fs_analyzer.cpp:9323-9349): Lua-table keys
	// are constants produced by the parser, while Python-dictionary keys remain expressions.
	// Godot Dictionary supplies the same string/StringName key equivalence as Foundry's
	// StringLikeVariantComparator, so it also preserves the first value line for diagnostics.
	bool all_constant = true;
	Dictionary values;
	Dictionary first_value_lines;
	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		const auto &element = p_dictionary->elements[i];
		if (p_dictionary->style == BSParser::DictionaryNode::PYTHON_DICT) {
			reduce_expression(element.key);
		}
		reduce_expression(element.value);

		if (element.key != nullptr && element.key->is_constant) {
			if (first_value_lines.has(element.key->reduced_value)) {
				push_error(vformat(R"(Key "%s" was already used in this dictionary (at line %d).)", element.key->reduced_value,
								   int(first_value_lines[element.key->reduced_value])),
						element.key);
			} else {
				first_value_lines[element.key->reduced_value] = element.value != nullptr ? element.value->start_line : element.key->start_line;
			}
		}
		if (element.key == nullptr || element.value == nullptr || !element.key->is_constant || !element.value->is_constant) {
			all_constant = false;
		} else if (!values.has(element.key->reduced_value)) {
			values[element.key->reduced_value] = element.value->reduced_value;
		}
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = Variant::DICTIONARY;
	if (all_constant) {
		p_dictionary->is_constant = true;
		p_dictionary->reduced = true;
		p_dictionary->reduced_value = values;
		type.is_constant = true;
	}
	p_dictionary->set_datatype(type);
}

void BSAnalyzer::reduce_ternary(BSParser::TernaryOpNode *p_ternary) {
	if (p_ternary == nullptr) {
		return;
	}
	reduce_expression(p_ternary->condition);
	reduce_expression(p_ternary->true_expr);
	reduce_expression(p_ternary->false_expr);
	if (p_ternary->condition != nullptr && p_ternary->condition->is_constant) {
		const bool take_true = p_ternary->condition->reduced_value.booleanize();
		BSParser::ExpressionNode *chosen = take_true ? p_ternary->true_expr : p_ternary->false_expr;
		if (chosen != nullptr && chosen->is_constant) {
			p_ternary->is_constant = true;
			p_ternary->reduced = true;
			p_ternary->reduced_value = chosen->reduced_value;
			p_ternary->set_datatype(type_from_variant(chosen->reduced_value));
			return;
		}
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_ternary->set_datatype(type);
}

void BSAnalyzer::reduce_cast(BSParser::CastNode *p_cast) {
	// Foundry reduce_cast @ c9d5e35: the cast type names what the operand is expected to be, so it
	// qualifies a contextual `.Case` shorthand (and nested container elements) in operand position.
	if (p_cast == nullptr) {
		return;
	}
	reduce_expression(p_cast->operand);

	BSParser::DataType cast_type = datatype_from_type_node(p_cast->cast_type);
	if (!cast_type.is_set()) {
		return;
	}
	if (cast_type.is_union()) {
		// A cast is a runtime operation and the runtime has no union carrier.
		push_error(vformat(R"(Cannot cast to the type union "%s", because it has no runtime type. Cast to one of its alternatives instead.)", cast_type.to_string()), p_cast->cast_type);
		return;
	}

	qualify_contextual_enum_case_consumer(p_cast->operand, cast_type);
	p_cast->set_datatype(cast_type);
	if (p_cast->operand != nullptr && p_cast->operand->is_constant) {
		p_cast->is_constant = true;
		p_cast->reduced = true;
		p_cast->reduced_value = p_cast->operand->reduced_value;
	}
}

void BSAnalyzer::reduce_type_test(BSParser::TypeTestNode *p_type_test) {
	// Foundry reduce_type_test @ c9d5e35: resolve the tested type (including contextual
	// `.Case` shorthand against the operand) and type case-bind payload identifiers.
	// Constant folding of non-enum type tests remains #60.
	if (p_type_test == nullptr) {
		return;
	}
	BSParser::DataType result;
	result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	result.kind = BSParser::DataType::BUILTIN;
	result.builtin_type = Variant::BOOL;
	p_type_test->set_datatype(result);

	if (p_type_test->operand == nullptr || p_type_test->test_type == nullptr) {
		return;
	}

	const int errors_before_operand = parser != nullptr ? parser->get_errors().size() : 0;
	reduce_expression(p_type_test->operand);
	const bool operand_errored = parser != nullptr && parser->get_errors().size() > errors_before_operand;
	BSParser::DataType operand_type = p_type_test->operand->get_datatype();

	BSParser::DataType test_type;
	if (p_type_test->test_type->is_contextual_enum_case) {
		// `x is .Ok(value)`: qualify against the operand's own union.
		const StringName case_name = p_type_test->test_type->type_chain.is_empty()
				? StringName()
				: p_type_test->test_type->type_chain[0]->name;
		BSParser::DataType case_meta_type;
		if (case_name == StringName()) {
			// Parser already reported a missing case name.
		} else if (operand_type.is_set() &&
				resolve_contextual_case_pattern_type(case_name, &operand_type, R"("is" operand)", p_type_test->test_type, case_meta_type, operand_errored)) {
			p_type_test->test_type->set_datatype(case_meta_type);
			test_type = type_from_metatype(case_meta_type);
		}
	} else {
		test_type = datatype_from_type_node(p_type_test->test_type);
		test_type.is_meta_type = false;
	}
	p_type_test->test_datatype = test_type;

	if (test_type.is_union()) {
		push_error(vformat(R"(Cannot test against the type union "%s", because it has no runtime type. Test one of its alternatives instead.)", test_type.to_string()), p_type_test->test_type);
		test_type = BSParser::DataType();
		p_type_test->test_datatype = test_type;
	}

	if (!operand_type.is_set() || !test_type.is_set()) {
		for (BSParser::IdentifierNode *bind : p_type_test->case_binds) {
			if (bind != nullptr) {
				BSParser::DataType bind_type;
				bind_type.kind = BSParser::DataType::VARIANT;
				bind_type.type_source = BSParser::DataType::INFERRED;
				bind->set_datatype(bind_type);
			}
		}
		return;
	}

	resolve_type_test_case_binds(p_type_test, test_type);
}

void BSAnalyzer::resolve_type_test_case_binds(BSParser::TypeTestNode *p_type_test, const BSParser::DataType &p_test_type) {
	if (p_type_test == nullptr || p_type_test->case_binds.is_empty()) {
		return;
	}

	const BSParser::DataType::EnumCasePayload *payload = nullptr;
	const bool is_enum_case_test = p_test_type.is_tagged_union_type() && p_test_type.enum_case_name != StringName();

	if (!is_enum_case_test) {
		push_error(R"*(Only a tagged-union case can bind payload values, e.g. "value is Message.Move(x, y)".)*", p_type_test);
	} else {
		if (!p_type_test->binds_allowed) {
			push_error(R"(Case payload binds are only allowed in the condition of "if", "elif", "while" or "assert", directly or as an "and" operand.)", p_type_test);
		}
		payload = p_test_type.get_enum_case_payload(p_test_type.enum_case_name);
		if (payload == nullptr) {
			push_error(vformat(R"(Case "%s" carries no payload, so it cannot bind values.)", p_test_type.enum_case_name), p_type_test);
		} else if (payload->field_types.size() != p_type_test->case_binds.size()) {
			push_error(vformat(R"(Case "%s" carries %d payload value(s), but %d bind(s) were given.)", p_test_type.enum_case_name, payload->field_types.size(), p_type_test->case_binds.size()), p_type_test);
			payload = nullptr;
		}
	}

	for (int i = 0; i < p_type_test->case_binds.size(); i++) {
		BSParser::IdentifierNode *bind = p_type_test->case_binds[i];
		if (bind == nullptr) {
			continue;
		}
		BSParser::DataType bind_type;
		if (payload != nullptr && i < payload->field_types.size()) {
			bind_type = complete_self_referential_enum_type(payload->field_types[i]);
		} else {
			bind_type.kind = BSParser::DataType::VARIANT;
			bind_type.type_source = BSParser::DataType::INFERRED;
		}
		bind->set_datatype(bind_type);
	}
}

void BSAnalyzer::analyze_if(BSParser::IfNode *p_if) {
	if (p_if == nullptr) {
		return;
	}
	// Foundry resolve_if @ c9d5e35: reduce the condition, then overlay true/false narrowing on each arm.
	flow_finality.reduce_condition_expression(p_if->condition);

	HashMap<const BSParser::Node *, BSParser::DataType> previous_flow_narrowed_types(flow_finality.get_flow_narrowed_types());
	flow_finality.apply_flow_narrowing_from_condition(p_if->condition, true);
	analyze_suite(p_if->true_block);
	flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;

	if (p_if->false_block != nullptr) {
		previous_flow_narrowed_types = flow_finality.get_flow_narrowed_types();
		flow_finality.apply_flow_narrowing_from_condition(p_if->condition, false);
		if (BSParser::IfNode *elif = p_if->get_elif()) {
			analyze_if(elif);
		} else {
			analyze_suite(p_if->false_block);
		}
		flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
	}
}

void BSAnalyzer::resolve_match(BSParser::MatchNode *p_match) {
	if (p_match == nullptr) {
		return;
	}
	// Foundry resolve_match @ c9d5e35 (match-branch narrowing slice).
	// A subject that failed to resolve still carries the Variant fallback its reduction left
	// behind, which reads exactly like a written-out Variant. The error delta is what tells the
	// two apart, so a contextual shorthand arm does not report the subject's failure a second
	// time per arm.
	const int errors_before_subject = parser != nullptr ? parser->get_errors().size() : 0;
	reduce_expression(p_match->test);
	bool subject_errored = parser != nullptr && parser->get_errors().size() > errors_before_subject;
	for (int i = 0; i < p_match->branches.size(); i++) {
		resolve_match_branch(p_match->branches[i], p_match->test, subject_errored);
	}
	check_match_exhaustiveness(p_match);
}

void BSAnalyzer::resolve_match_branch(BSParser::MatchBranchNode *p_match_branch, BSParser::ExpressionNode *p_match_test, bool p_subject_errored) {
	if (p_match_branch == nullptr) {
		return;
	}
	for (BSParser::AnnotationNode *annotation : p_match_branch->annotations) {
		if (annotation != nullptr) {
			// Match-branch annotations have no dedicated TargetKind in the v1 set; resolve args then apply.
			resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_NONE);
			annotation->apply(parser, p_match_branch, current_class);
		}
	}

	for (int i = 0; i < p_match_branch->patterns.size(); i++) {
		resolve_match_pattern(p_match_branch->patterns[i], p_match_test, nullptr, p_subject_errored);
	}

	HashMap<const BSParser::Node *, BSParser::DataType> previous_flow_narrowed_types(flow_finality.get_flow_narrowed_types());
	flow_finality.apply_match_branch_flow_narrowing(p_match_test, p_match_branch);

	if (p_match_branch->guard_body != nullptr) {
		analyze_suite(p_match_branch->guard_body);
	}
	analyze_suite(p_match_branch->block);
	flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
}

void BSAnalyzer::resolve_match_pattern(BSParser::PatternNode *p_match_pattern, BSParser::ExpressionNode *p_match_test, const BSParser::DataType *p_match_test_type, bool p_subject_errored) {
	if (p_match_pattern == nullptr) {
		return;
	}

	BSParser::DataType match_test_type;
	bool has_match_test_type = false;
	if (p_match_test != nullptr) {
		match_test_type = p_match_test->get_datatype();
		has_match_test_type = match_test_type.is_set();
	} else if (p_match_test_type != nullptr) {
		match_test_type = *p_match_test_type;
		has_match_test_type = match_test_type.is_set();
	}

	BSParser::DataType result;
	switch (p_match_pattern->pattern_type) {
		case BSParser::PatternNode::PT_LITERAL:
			if (p_match_pattern->literal != nullptr) {
				reduce_literal(p_match_pattern->literal);
				result = p_match_pattern->literal->get_datatype();
			}
			break;
		case BSParser::PatternNode::PT_EXPRESSION:
			if (p_match_pattern->expression != nullptr) {
				BSParser::ExpressionNode *expr = p_match_pattern->expression;
				// `.Quit`: a payload-less contextual case arrives as an expression pattern and
				// takes its union from the match subject (Foundry @ c9d5e35).
				if (resolve_contextual_case_value_pattern(expr, has_match_test_type ? &match_test_type : nullptr, p_subject_errored)) {
					result = expr->get_datatype();
					break;
				}
				reduce_expression(expr);
				result = expr->get_datatype();

				// Bare native class names in match patterns are type patterns (`match v: Node:`).
				// Foundry reduce_identifier publishes NATIVE meta types only after locals/params/
				// members fail to bind; gate ClassDB promotion the same way so a shadowed name
				// stays a value pattern.
				if (expr->type == BSParser::Node::IDENTIFIER && !result.is_meta_type) {
					BSParser::IdentifierNode *identifier = static_cast<BSParser::IdentifierNode *>(expr);
					if (identifier->source == BSParser::IdentifierNode::UNDEFINED_SOURCE &&
							ClassDB::class_exists(identifier->name)) {
						BSParser::DataType meta;
						meta.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
						meta.kind = BSParser::DataType::NATIVE;
						meta.builtin_type = Variant::OBJECT;
						meta.native_type = identifier->name;
						meta.is_constant = true;
						meta.is_meta_type = true;
						identifier->source = BSParser::IdentifierNode::NATIVE_CLASS;
						identifier->set_datatype(meta);
						result = meta;
					}
				}

				// `value is T` where the operand names the same identifier as the match subject.
				if (expr->type == BSParser::Node::TYPE_TEST && p_match_test != nullptr && p_match_test->type == BSParser::Node::IDENTIFIER) {
					const BSParser::TypeTestNode *type_test = static_cast<const BSParser::TypeTestNode *>(expr);
					if (type_test->operand != nullptr && type_test->operand->type == BSParser::Node::IDENTIFIER) {
						const BSParser::IdentifierNode *pattern_operand = static_cast<const BSParser::IdentifierNode *>(type_test->operand);
						const BSParser::IdentifierNode *match_identifier = static_cast<const BSParser::IdentifierNode *>(p_match_test);
						p_match_pattern->is_subject_type_test = pattern_operand->name == match_identifier->name;
					}
				}
			}
			break;
		case BSParser::PatternNode::PT_WILDCARD:
			p_match_pattern->is_irrefutable = true;
			result.kind = BSParser::DataType::VARIANT;
			break;
		case BSParser::PatternNode::PT_BIND:
			if (has_match_test_type) {
				result = match_test_type;
			} else {
				result.kind = BSParser::DataType::VARIANT;
			}
			p_match_pattern->is_irrefutable = true;
			if (p_match_pattern->bind != nullptr) {
				p_match_pattern->bind->set_datatype(result);
			}
			break;
		case BSParser::PatternNode::PT_ARRAY:
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				BSParser::DataType element_type;
				const BSParser::DataType *element_type_ptr = nullptr;
				if (has_match_test_type && match_test_type.kind == BSParser::DataType::BUILTIN &&
						match_test_type.builtin_type == Variant::ARRAY && match_test_type.has_container_element_type(0)) {
					element_type = match_test_type.get_container_element_type(0);
					element_type_ptr = &element_type;
				}
				resolve_match_pattern(p_match_pattern->array[i], nullptr, element_type_ptr, p_subject_errored);
			}
			result = p_match_pattern->get_datatype();
			break;
		case BSParser::PatternNode::PT_DICTIONARY:
			for (int i = 0; i < p_match_pattern->dictionary.size(); i++) {
				if (p_match_pattern->dictionary[i].key != nullptr) {
					reduce_expression(p_match_pattern->dictionary[i].key);
					if (!p_match_pattern->dictionary[i].key->is_constant) {
						push_error(R"(Expression in dictionary pattern key must be a constant.)", p_match_pattern->dictionary[i].key);
					}
				}
				if (p_match_pattern->dictionary[i].value_pattern != nullptr) {
					BSParser::DataType value_type;
					const BSParser::DataType *value_type_ptr = nullptr;
					if (has_match_test_type && match_test_type.kind == BSParser::DataType::BUILTIN &&
							match_test_type.builtin_type == Variant::DICTIONARY && match_test_type.has_container_element_type(1)) {
						value_type = match_test_type.get_container_element_type(1);
						value_type_ptr = &value_type;
					}
					resolve_match_pattern(p_match_pattern->dictionary[i].value_pattern, nullptr, value_type_ptr, p_subject_errored);
				}
			}
			result = p_match_pattern->get_datatype();
			break;
		case BSParser::PatternNode::PT_TUPLE: {
			const bool subject_is_tuple = has_match_test_type && match_test_type.is_tuple();
			if (subject_is_tuple && match_test_type.get_container_element_type_count() != p_match_pattern->array.size()) {
				push_error(vformat(R"(Tuple pattern has %d element(s), but "%s" has %d.)", p_match_pattern->array.size(), match_test_type.to_string(), match_test_type.get_container_element_type_count()), p_match_pattern);
			}

			bool all_irrefutable = true;
			for (int i = 0; i < p_match_pattern->array.size(); i++) {
				BSParser::DataType element_type;
				const BSParser::DataType *element_type_ptr = nullptr;
				if (subject_is_tuple && match_test_type.has_container_element_type(i)) {
					element_type = match_test_type.get_container_element_type(i);
					element_type_ptr = &element_type;
				}
				resolve_match_pattern(p_match_pattern->array[i], nullptr, element_type_ptr, p_subject_errored);
				all_irrefutable = all_irrefutable && p_match_pattern->array[i] != nullptr && p_match_pattern->array[i]->is_irrefutable;
			}
			p_match_pattern->is_irrefutable = all_irrefutable && subject_is_tuple && !match_test_type.is_nullable &&
					match_test_type.get_container_element_type_count() == p_match_pattern->array.size();
			result = p_match_pattern->get_datatype();
		} break;
		case BSParser::PatternNode::PT_ENUM_CASE:
			resolve_match_case_pattern(p_match_pattern, has_match_test_type ? &match_test_type : nullptr, p_subject_errored);
			result = p_match_pattern->case_datatype;
			break;
		case BSParser::PatternNode::PT_REST:
			result.kind = BSParser::DataType::VARIANT;
			break;
	}

	p_match_pattern->set_datatype(result);
}

void BSAnalyzer::resolve_match_case_pattern(BSParser::PatternNode *p_match_pattern, const BSParser::DataType *p_match_test_type, bool p_subject_errored) {
	if (p_match_pattern == nullptr) {
		return;
	}

	BSParser::DataType case_type;
	bool case_type_failed = false;
	if (p_match_pattern->is_contextual_enum_case) {
		const StringName case_name = p_match_pattern->case_type != nullptr && !p_match_pattern->case_type->type_chain.is_empty()
				? p_match_pattern->case_type->type_chain[0]->name
				: StringName();
		BSParser::DataType case_meta_type;
		case_type_failed = case_name == StringName() ||
				!resolve_contextual_case_pattern_type(case_name, p_match_test_type, "match subject", p_match_pattern, case_meta_type, p_subject_errored);
		if (!case_type_failed) {
			// Publish the subject-supplied union on the head, as the qualified spelling does.
			p_match_pattern->case_type->set_datatype(case_meta_type);
			case_type = type_from_metatype(case_meta_type);
		}
	} else {
		const int errors_before_case_type = parser != nullptr ? parser->get_errors().size() : 0;
		case_type = datatype_from_type_node(p_match_pattern->case_type);
		case_type_failed = parser != nullptr && parser->get_errors().size() > errors_before_case_type;
	}
	p_match_pattern->case_datatype = case_type;

	const BSParser::DataType::EnumCasePayload *payload = nullptr;
	if (case_type.is_set() && !case_type_failed) {
		if (!case_type.is_tagged_union_type() || case_type.enum_case_name == StringName()) {
			push_error(R"*(Only a tagged-union case can match payload values, e.g. "Message.Move(x, y)".)*", p_match_pattern);
		} else {
			BSParser::DataType union_type = case_type;
			union_type.enum_case_name = StringName();
			if (p_match_test_type != nullptr && p_match_test_type->is_hard_type() &&
					!BSTypeCompatibility::is_compatible(union_type, *p_match_test_type) &&
					!BSTypeCompatibility::is_compatible(*p_match_test_type, union_type)) {
				push_error(vformat(R"(Pattern matches a case of "%s", but the "match" subject is of type "%s".)", union_type.to_string(), p_match_test_type->to_string()), p_match_pattern);
			}

			payload = case_type.get_enum_case_payload(case_type.enum_case_name);
			if (payload == nullptr) {
				push_error(vformat(R"(Case "%s" carries no payload, so it cannot match payload values.)", case_type.enum_case_name), p_match_pattern);
			} else if (payload->field_types.size() != p_match_pattern->array.size()) {
				push_error(vformat(R"(Case "%s" carries %d payload value(s), but %d pattern(s) were given.)", case_type.enum_case_name, payload->field_types.size(), p_match_pattern->array.size()), p_match_pattern);
				payload = nullptr;
			}
		}
	}

	bool all_irrefutable = true;
	for (int i = 0; i < p_match_pattern->array.size(); i++) {
		BSParser::DataType field_type;
		const BSParser::DataType *field_type_ptr = nullptr;
		if (payload != nullptr && i < payload->field_types.size()) {
			field_type = complete_self_referential_enum_type(payload->field_types[i]);
			field_type_ptr = &field_type;
		}
		resolve_match_pattern(p_match_pattern->array[i], nullptr, field_type_ptr, p_subject_errored);
		all_irrefutable = all_irrefutable && p_match_pattern->array[i] != nullptr && p_match_pattern->array[i]->is_irrefutable;
	}
	p_match_pattern->is_irrefutable = false;
	p_match_pattern->case_payload_is_irrefutable = payload != nullptr && all_irrefutable;
}

bool BSAnalyzer::tagged_union_metatype_from_expected_type(const BSParser::DataType &p_expected_type,
		const BSParser::Node *p_source, BSParser::DataType &r_enum_meta_type) {
	(void)p_source;
	if (!p_expected_type.is_set() || p_expected_type.has_no_type() || p_expected_type.is_meta_type ||
			p_expected_type.kind != BSParser::DataType::ENUM || !p_expected_type.is_tagged_union) {
		return false;
	}

	// Inverse of type_from_metatype() for a tagged union: publish the same case/payload schema
	// as the union meta type the construction / pattern head expects.
	BSParser::DataType enum_meta_type = p_expected_type;
	enum_meta_type.is_meta_type = true;
	enum_meta_type.is_pseudo_type = false;
	enum_meta_type.is_constant = false;
	enum_meta_type.is_read_only = false;
	enum_meta_type.is_nullable = false;
	enum_meta_type.enum_case_name = StringName();
	enum_meta_type.builtin_type = Variant::DICTIONARY;

	// Generic unions remain M5; an unbound specialization cannot type a payload.
	if (enum_meta_type.has_type_arguments()) {
		return false;
	}

	r_enum_meta_type = enum_meta_type;
	return true;
}

bool BSAnalyzer::resolve_contextual_case_pattern_type(const StringName &p_case_name, const BSParser::DataType *p_subject_type,
		const char *p_subject_description, const BSParser::Node *p_source, BSParser::DataType &r_case_meta_type,
		bool p_subject_errored) {
	BSParser::DataType enum_meta_type;
	if (p_subject_type == nullptr || !tagged_union_metatype_from_expected_type(*p_subject_type, p_source, enum_meta_type)) {
		if (p_subject_errored) {
			return false;
		}
		const String subject_type_name = p_subject_type != nullptr && p_subject_type->is_set()
				? p_subject_type->to_string()
				: String("Variant");
		push_error(vformat(R"*(Contextual shorthand ".%s" needs a tagged-union %s, but it is of type "%s".)*",
						   p_case_name, p_subject_description, subject_type_name),
				p_source);
		return false;
	}

	if (!enum_meta_type.enum_values.has(p_case_name)) {
		push_error(vformat(R"(Tagged union "%s" has no case "%s".)", enum_meta_type.enum_type, p_case_name), p_source);
		return false;
	}

	enum_meta_type.enum_case_name = p_case_name;
	r_case_meta_type = enum_meta_type;
	return true;
}

bool BSAnalyzer::resolve_contextual_case_value_pattern(BSParser::ExpressionNode *p_expression, const BSParser::DataType *p_match_test_type, bool p_subject_errored) {
	if (p_expression == nullptr || p_expression->type != BSParser::Node::SUBSCRIPT) {
		return false;
	}
	BSParser::SubscriptNode *reference = static_cast<BSParser::SubscriptNode *>(p_expression);
	if (!reference->is_contextual_enum_case) {
		return false;
	}

	BSParser::DataType unqualified_type;
	unqualified_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	unqualified_type.kind = BSParser::DataType::VARIANT;

	if (reference->attribute == nullptr) {
		p_expression->set_datatype(unqualified_type);
		return true;
	}
	const StringName case_name = reference->attribute->name;

	BSParser::DataType case_meta_type;
	if (!resolve_contextual_case_pattern_type(case_name, p_match_test_type, "match subject", p_expression, case_meta_type, p_subject_errored)) {
		p_expression->set_datatype(unqualified_type);
		return true;
	}

	const BSParser::DataType case_value_type = type_from_metatype(case_meta_type);
	if (case_meta_type.get_enum_case_payload(case_name) != nullptr) {
		push_error(vformat(R"*(Case "%s" carries a payload, so it is matched with payload patterns, e.g. ".%s(...)".)*",
						   case_name, case_name),
				p_expression);
		p_expression->set_datatype(case_value_type);
		return true;
	}

	reference->attribute->set_datatype(case_value_type);
	reference->set_datatype(case_value_type);
	reference->is_constant = true;
	reference->reduced_value = _tagged_union_case_singleton(case_meta_type.enum_values[case_name]);
	return true;
}

// Foundry contextual_enum_case_reference @ c9d5e35: `.Quit` is a bare subscript; `.Move(1, 2)`
// is that subscript with a call suffix.
static BSParser::SubscriptNode *_contextual_enum_case_reference(BSParser::ExpressionNode *p_expression, BSParser::CallNode **r_call) {
	*r_call = nullptr;
	if (p_expression == nullptr) {
		return nullptr;
	}
	if (p_expression->type == BSParser::Node::CALL) {
		BSParser::CallNode *call = static_cast<BSParser::CallNode *>(p_expression);
		if (!call->is_contextual_enum_case || call->callee == nullptr || call->callee->type != BSParser::Node::SUBSCRIPT) {
			return nullptr;
		}
		*r_call = call;
		return static_cast<BSParser::SubscriptNode *>(call->callee);
	}
	if (p_expression->type == BSParser::Node::SUBSCRIPT) {
		BSParser::SubscriptNode *reference = static_cast<BSParser::SubscriptNode *>(p_expression);
		return reference->is_contextual_enum_case ? reference : nullptr;
	}
	return nullptr;
}

void BSAnalyzer::register_contextual_enum_case(BSParser::ExpressionNode *p_expression) {
	reduced_contextual_enum_cases.push_back(p_expression);
}

bool BSAnalyzer::contextual_enum_case_awaits_expected_type(BSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return false;
	}
	if (p_expression->type == BSParser::Node::TERNARY_OPERATOR) {
		BSParser::TernaryOpNode *ternary = static_cast<BSParser::TernaryOpNode *>(p_expression);
		return contextual_enum_case_awaits_expected_type(ternary->true_expr) ||
				contextual_enum_case_awaits_expected_type(ternary->false_expr);
	}
	BSParser::CallNode *call = nullptr;
	return _contextual_enum_case_reference(p_expression, &call) != nullptr &&
			!resolved_contextual_enum_cases.has(p_expression);
}

void BSAnalyzer::report_unqualified_contextual_enum_cases() {
	for (BSParser::ExpressionNode *expression : reduced_contextual_enum_cases) {
		resolve_contextual_enum_case(expression, BSParser::DataType());
	}
	reduced_contextual_enum_cases.clear();
}

void BSAnalyzer::qualify_contextual_enum_case_consumer(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type) {
	// Foundry consumer sites call resolve_contextual_enum_case then update_container_literal_element_types.
	resolve_contextual_enum_case(p_expression, p_expected_type);
	update_container_literal_element_types(p_expression, p_expected_type);
}

bool BSAnalyzer::update_constant_expression_type(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type, const char *p_usage) {
	if (p_expression == nullptr || !p_expression->is_constant || !p_expected_type.is_set() || p_expected_type.is_variant()) {
		return true;
	}
	const BSParser::DataType declared_type = p_expression->get_datatype();
	// Concrete literals are described by their ordinary call/assign/return consumer. A gradual
	// constant or closed union needs this value-aware path because the consumer otherwise admits
	// Variant or loses the known alternative. Container elements use their pinned "include" wording.
	if (!declared_type.is_variant() && declared_type.kind != BSParser::DataType::UNION && String(p_usage) != "include") {
		return true;
	}
	BSParser::DataType comparison_type = declared_type;
	if (declared_type.is_variant()) {
		comparison_type = type_from_variant(p_expression->reduced_value);
	}
	BSTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = strict_dynamic_checks;
	options.strict_null = strict_null_checks;
	options.constant_source_value = &p_expression->reduced_value;
	if (!BSTypeCompatibility::check(p_expected_type, comparison_type, options).compatible) {
		const BSParser::DataType &reported_type = declared_type.is_variant() ? comparison_type : declared_type;
		push_error(vformat(R"*(Cannot %s a value of type "%s" as "%s".)*",
						   p_usage, reported_type.to_string(), p_expected_type.to_string()),
				p_expression);
		return false;
	}
	if (p_expected_type.kind == BSParser::DataType::BUILTIN && comparison_type.kind == BSParser::DataType::BUILTIN) {
		if (p_expected_type.builtin_type == comparison_type.builtin_type) {
			BSParser::DataType published = p_expected_type;
			published.is_constant = true;
			p_expression->set_datatype(published);
		}
	}
	return true;
}

bool BSAnalyzer::update_container_literal_element_types(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type) {
	if (p_expression == nullptr || !p_expected_type.is_set() || !p_expected_type.is_hard_type()) {
		return false;
	}
	BSParser::DataType target_type = p_expected_type;
	if (target_type.kind == BSParser::DataType::UNION) {
		Vector<BSParser::DataType> claimants;
		for (const BSParser::DataType &member : target_type.union_members) {
			const bool claims = (p_expression->type == BSParser::Node::ARRAY && member.kind == BSParser::DataType::BUILTIN && member.builtin_type == Variant::ARRAY) ||
					(p_expression->type == BSParser::Node::DICTIONARY && member.kind == BSParser::DataType::BUILTIN && member.builtin_type == Variant::DICTIONARY) ||
					(p_expression->type == BSParser::Node::TUPLE_LITERAL && member.kind == BSParser::DataType::TUPLE && member.tuple_name == StringName() && !member.is_meta_type);
			if (claims) {
				claimants.push_back(member);
			}
		}
		if (claimants.size() > 1) {
			bool every_claimant_names_self = true;
			for (const BSParser::DataType &claimant : claimants) {
				every_claimant_names_self = every_claimant_names_self && _datatype_contains_self_type_parameter(claimant);
			}
			if (!every_claimant_names_self) {
				return false;
			}

			// Foundry container_literal_elements_could_fit @ c9d5e35: disambiguate
			// only when every shape claimant needs Self and exactly one accepts the
			// already-reduced elements. This pass is read-only and reports nothing.
			auto candidate_could_fit = [&](auto &&self, const BSParser::ExpressionNode *expression,
											   const BSParser::DataType &candidate) -> bool {
				auto element_could_fit = [&](const BSParser::ExpressionNode *element,
												 const BSParser::DataType &expected) -> bool {
					if (element == nullptr || !expected.is_set() || !expected.is_hard_type() ||
							expected.is_variant() || expected.kind == BSParser::DataType::UNION) {
						return true;
					}
					if (element->type == BSParser::Node::ARRAY || element->type == BSParser::Node::DICTIONARY ||
							element->type == BSParser::Node::TUPLE_LITERAL) {
						const bool shape_matches =
								(element->type == BSParser::Node::ARRAY && expected.kind == BSParser::DataType::BUILTIN && expected.builtin_type == Variant::ARRAY) ||
								(element->type == BSParser::Node::DICTIONARY && expected.kind == BSParser::DataType::BUILTIN && expected.builtin_type == Variant::DICTIONARY) ||
								(element->type == BSParser::Node::TUPLE_LITERAL && expected.kind == BSParser::DataType::TUPLE && expected.tuple_name == StringName() && !expected.is_meta_type);
						return shape_matches && self(self, element, expected);
					}
					const BSParser::DataType actual = element->get_datatype();
					if (!actual.is_set() || !actual.is_hard_type() || actual.is_variant()) {
						return true;
					}
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					if (element->is_constant) {
						options.constant_source_value = &element->reduced_value;
					}
					return BSTypeCompatibility::check(_substitute_self_type_parameter_with_bounds(expected, false),
							_substitute_self_type_parameter_with_bounds(actual, false), options)
							.compatible;
				};

				switch (expression->type) {
					case BSParser::Node::ARRAY: {
						if (!candidate.has_container_element_type(0)) {
							return true;
						}
						const BSParser::ArrayNode *array = static_cast<const BSParser::ArrayNode *>(expression);
						for (const BSParser::ExpressionNode *element : array->elements) {
							if (!element_could_fit(element, candidate.get_container_element_type(0))) {
								return false;
							}
						}
						return true;
					}
					case BSParser::Node::DICTIONARY: {
						if (!candidate.has_container_element_types()) {
							return true;
						}
						const BSParser::DictionaryNode *dictionary = static_cast<const BSParser::DictionaryNode *>(expression);
						for (const BSParser::DictionaryNode::Pair &element : dictionary->elements) {
							if (!element_could_fit(element.key, candidate.get_container_element_type_or_variant(0)) ||
									!element_could_fit(element.value, candidate.get_container_element_type_or_variant(1))) {
								return false;
							}
						}
						return true;
					}
					case BSParser::Node::TUPLE_LITERAL: {
						const BSParser::TupleLiteralNode *tuple = static_cast<const BSParser::TupleLiteralNode *>(expression);
						if (tuple->elements.size() != candidate.container_element_types.size()) {
							return false;
						}
						for (int i = 0; i < tuple->elements.size(); i++) {
							if (!element_could_fit(tuple->elements[i], candidate.container_element_types[i])) {
								return false;
							}
						}
						return true;
					}
					default:
						return false;
				}
			};

			int unique_fit = -1;
			for (int i = 0; i < claimants.size(); i++) {
				if (!candidate_could_fit(candidate_could_fit, p_expression, claimants[i])) {
					continue;
				}
				if (unique_fit >= 0) {
					return false;
				}
				unique_fit = i;
			}
			if (unique_fit < 0) {
				return false;
			}
			target_type = claimants[unique_fit];
		} else if (claimants.size() == 1) {
			target_type = claimants[0];
		} else {
			return false;
		}
	}

	switch (p_expression->type) {
		case BSParser::Node::ARRAY: {
			if (target_type.kind == BSParser::DataType::BUILTIN && target_type.builtin_type == Variant::ARRAY &&
					target_type.has_container_element_type(0)) {
				const int error_count = parser != nullptr ? parser->get_errors().size() : 0;
				update_array_literal_element_type(static_cast<BSParser::ArrayNode *>(p_expression),
						target_type.get_container_element_type(0));
				if (parser != nullptr && parser->get_errors().size() > error_count) {
					return true;
				}
				BSParser::DataType published = datatype_contains_self_type_parameter(target_type)
						? _substitute_self_type_parameter_with_bounds(target_type, true)
						: target_type;
				published.is_constant = p_expression->is_constant;
				p_expression->set_datatype(published);
				return true;
			}
		} break;
		case BSParser::Node::DICTIONARY: {
			if (target_type.kind == BSParser::DataType::BUILTIN && target_type.builtin_type == Variant::DICTIONARY &&
					target_type.has_container_element_types()) {
				const int error_count = parser != nullptr ? parser->get_errors().size() : 0;
				update_dictionary_literal_element_type(static_cast<BSParser::DictionaryNode *>(p_expression),
						target_type.get_container_element_type_or_variant(0),
						target_type.get_container_element_type_or_variant(1));
				if (parser != nullptr && parser->get_errors().size() > error_count) {
					return true;
				}
				BSParser::DataType published = datatype_contains_self_type_parameter(target_type)
						? _substitute_self_type_parameter_with_bounds(target_type, true)
						: target_type;
				published.is_constant = p_expression->is_constant;
				p_expression->set_datatype(published);
				return true;
			}
		} break;
		case BSParser::Node::TUPLE_LITERAL: {
			if (target_type.kind != BSParser::DataType::TUPLE || target_type.tuple_name != StringName() || target_type.is_meta_type) {
				break;
			}
			BSParser::TupleLiteralNode *literal = static_cast<BSParser::TupleLiteralNode *>(p_expression);
			if (literal->elements.size() != target_type.container_element_types.size()) {
				return false;
			}
			Vector<BSParser::DataType> element_types;
			for (int i = 0; i < literal->elements.size(); i++) {
				BSParser::ExpressionNode *element = literal->elements[i];
				const BSParser::DataType expected_element = target_type.get_container_element_type(i);
				resolve_contextual_enum_case(element, expected_element);
				update_container_literal_element_types(element, expected_element);
				update_constant_expression_type(element, expected_element, "include");
				element_types.push_back(element->get_datatype());
			}
			BSParser::DataType published = make_tuple_type(StringName(), String(), String(), element_types, Vector<StringName>(), false);
			published.is_constant = p_expression->is_constant;
			p_expression->set_datatype(published);
			return true;
		} break;
		default:
			break;
	}
	return false;
}

void BSAnalyzer::update_array_literal_element_type(BSParser::ArrayNode *p_array, const BSParser::DataType &p_element_type) {
	if (p_array == nullptr) {
		return;
	}
	for (int i = 0; i < p_array->elements.size(); i++) {
		BSParser::ExpressionNode *element_node = p_array->elements[i];
		if (element_node == nullptr) {
			continue;
		}
		// An element stands where the container's element type says it stands.
		resolve_contextual_enum_case(element_node, p_element_type);
		update_container_literal_element_types(element_node, p_element_type);
		mark_coroutine_handle_capture(element_node, p_element_type);
		const bool constant_type_ok = update_constant_expression_type(element_node, p_element_type, "include");
		if (datatype_contains_self_type_parameter(p_element_type) && element_node->type == BSParser::Node::SELF) {
			element_node->set_datatype(_substitute_self_type_parameter_with_bounds(p_element_type, true));
		}
		const BSParser::DataType element_type = element_node->get_datatype();
		bool compatible = constant_type_ok;
		if (compatible && datatype_contains_self_type_parameter(p_element_type)) {
			compatible = element_node->type == BSParser::Node::SELF ||
					_datatype_matches_analyzer_substituted_self(p_element_type, element_type) ||
					_self_contract_admits_value_type(p_element_type, element_type, SelfContractKind::RETURN, element_node, nullptr);
		} else if (compatible) {
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = true;
			options.strict_dynamic = strict_dynamic_checks;
			options.strict_null = strict_null_checks;
			compatible = BSTypeCompatibility::check(p_element_type, element_type, options).compatible;
		}
		if (!compatible) {
			BSParser::DataType array_type;
			array_type.kind = BSParser::DataType::BUILTIN;
			array_type.builtin_type = Variant::ARRAY;
			array_type.container_element_types.push_back(p_element_type);
			push_error(vformat(R"*(Cannot have an element of type "%s" in an array of type "%s".)*",
							   element_type.to_string(), array_type.to_string()),
					element_node);
		}
	}
}

void BSAnalyzer::update_dictionary_literal_element_type(BSParser::DictionaryNode *p_dictionary, const BSParser::DataType &p_key_type, const BSParser::DataType &p_value_type) {
	if (p_dictionary == nullptr) {
		return;
	}
	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		BSParser::ExpressionNode *key_element_node = p_dictionary->elements[i].key;
		if (key_element_node != nullptr) {
			resolve_contextual_enum_case(key_element_node, p_key_type);
			update_container_literal_element_types(key_element_node, p_key_type);
			mark_coroutine_handle_capture(key_element_node, p_key_type);
			const bool constant_type_ok = update_constant_expression_type(key_element_node, p_key_type, "include");
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = true;
			options.strict_dynamic = strict_dynamic_checks;
			options.strict_null = strict_null_checks;
			const BSParser::DataType key_type = key_element_node->get_datatype();
			if (!constant_type_ok || !BSTypeCompatibility::check(p_key_type, key_type, options).compatible) {
				BSParser::DataType dictionary_type;
				dictionary_type.kind = BSParser::DataType::BUILTIN;
				dictionary_type.builtin_type = Variant::DICTIONARY;
				dictionary_type.container_element_types.push_back(p_key_type);
				dictionary_type.container_element_types.push_back(p_value_type);
				push_error(vformat(R"*(Cannot have a key of type "%s" in a dictionary of type "%s".)*",
								   key_type.to_string(), dictionary_type.to_string()),
						key_element_node);
			}
		}
		BSParser::ExpressionNode *value_element_node = p_dictionary->elements[i].value;
		if (value_element_node != nullptr) {
			resolve_contextual_enum_case(value_element_node, p_value_type);
			update_container_literal_element_types(value_element_node, p_value_type);
			mark_coroutine_handle_capture(value_element_node, p_value_type);
			const bool constant_type_ok = update_constant_expression_type(value_element_node, p_value_type, "include");
			if (datatype_contains_self_type_parameter(p_value_type) && value_element_node->type == BSParser::Node::SELF) {
				value_element_node->set_datatype(_substitute_self_type_parameter_with_bounds(p_value_type, true));
			}
			const BSParser::DataType value_type = value_element_node->get_datatype();
			bool compatible = constant_type_ok;
			if (compatible && datatype_contains_self_type_parameter(p_value_type)) {
				compatible = value_element_node->type == BSParser::Node::SELF ||
						_datatype_matches_analyzer_substituted_self(p_value_type, value_type) ||
						_self_contract_admits_value_type(p_value_type, value_type, SelfContractKind::RETURN, value_element_node, nullptr);
			} else if (compatible) {
				BSTypeCompatibility::Options options;
				options.allow_implicit_conversion = true;
				options.strict_dynamic = strict_dynamic_checks;
				options.strict_null = strict_null_checks;
				compatible = BSTypeCompatibility::check(p_value_type, value_type, options).compatible;
			}
			if (!compatible) {
				BSParser::DataType dictionary_type;
				dictionary_type.kind = BSParser::DataType::BUILTIN;
				dictionary_type.builtin_type = Variant::DICTIONARY;
				dictionary_type.container_element_types.push_back(p_key_type);
				dictionary_type.container_element_types.push_back(p_value_type);
				push_error(vformat(R"*(Cannot have a value of type "%s" in a dictionary of type "%s".)*",
								   value_type.to_string(), dictionary_type.to_string()),
						value_element_node);
			}
		}
	}
}

namespace {

// Foundry Self / Self-contract helpers @ c9d5e35 (enum SelfFieldLeg + RETURN assign/return).
// Generalized from the #127 enum-payload slice so assignable / return / assignment sites share
// the same Self admission rules rather than enum-only copies.

bool _is_self_type_parameter(const BSParser::DataType &p_type) {
	return p_type.kind == BSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_name == SNAME("@Self");
}

bool _is_bare_self_value_parameter(const BSParser::DataType &p_type) {
	return _is_self_type_parameter(p_type) && !p_type.is_type_handle_annotation;
}

BSParser::DataType _self_type_for_class(BSParser::ClassNode *p_class) {
	BSParser::DataType self_type;
	if (p_class == nullptr) {
		return self_type;
	}
	self_type = p_class->get_datatype();
	self_type.is_meta_type = false;
	self_type.type_arguments.clear();
	if (!self_type.is_set() || self_type.is_variant()) {
		self_type.kind = BSParser::DataType::CLASS;
		self_type.class_type = p_class;
		self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		self_type.builtin_type = Variant::OBJECT;
		self_type.native_type = p_class->base_type.native_type;
		self_type.script_path = p_class->fqcn.begins_with("res://") ? p_class->fqcn : String();
	}
	return self_type;
}

BSParser::DataType _self_type_parameter_from_bound(const BSParser::DataType &p_bound) {
	BSParser::DataType self_type;
	self_type.kind = BSParser::DataType::TYPE_PARAMETER;
	self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.type_parameter_name = SNAME("@Self");
	self_type.type_parameter_scope = BSParser::DataType::TYPE_PARAMETER_CLASS;
	self_type.type_parameter_index = -1;
	if (_is_bare_self_value_parameter(p_bound)) {
		if (!p_bound.type_parameter_bound.is_empty()) {
			self_type.type_parameter_bound.push_back(p_bound.type_parameter_bound[0]);
		}
		return self_type;
	}
	if (p_bound.is_set() && !p_bound.is_variant()) {
		self_type.type_parameter_bound.push_back(p_bound);
	}
	return self_type;
}

BSParser::DataType _self_type_parameter_for_class(BSParser::ClassNode *p_class) {
	return _self_type_parameter_from_bound(_self_type_for_class(p_class));
}

bool _datatype_contains_self_type_parameter(const BSParser::DataType &p_type) {
	if (_is_self_type_parameter(p_type)) {
		return true;
	}
	for (const BSParser::DataType &element : p_type.container_element_types) {
		if (_datatype_contains_self_type_parameter(element)) {
			return true;
		}
	}
	for (const BSParser::DataType &argument : p_type.type_arguments) {
		if (_datatype_contains_self_type_parameter(argument)) {
			return true;
		}
	}
	for (const BSParser::DataType &parameter_type : p_type.method_parameter_types) {
		if (_datatype_contains_self_type_parameter(parameter_type)) {
			return true;
		}
	}
	for (const BSParser::DataType &return_type : p_type.method_return_type) {
		if (_datatype_contains_self_type_parameter(return_type)) {
			return true;
		}
	}
	for (const BSParser::DataType &rest_parameter_type : p_type.method_rest_parameter_type) {
		if (_datatype_contains_self_type_parameter(rest_parameter_type)) {
			return true;
		}
	}
	for (const BSParser::DataType &member : p_type.union_members) {
		if (_datatype_contains_self_type_parameter(member)) {
			return true;
		}
	}
	return false;
}

BSParser::DataType _substitute_self_type_parameter(const BSParser::DataType &p_type, const BSParser::DataType &p_self_type) {
	if (!p_self_type.is_set()) {
		return p_type;
	}
	HashMap<StringName, BSParser::DataType> bindings;
	bindings.insert(SNAME("@Self"), p_self_type);
	return BSParser::DataType::substitute(p_type, bindings);
}

BSParser::DataType _substitute_self_type_parameter_with_bounds(const BSParser::DataType &p_type, bool p_mark_substitution = false) {
	if (_is_self_type_parameter(p_type)) {
		if (p_type.type_parameter_bound.is_empty()) {
			return p_type;
		}
		BSParser::DataType result = p_type.type_parameter_bound[0];
		if (p_type.is_type_handle_annotation) {
			result.is_meta_type = true;
			result.is_type_handle_annotation = true;
			result.is_pseudo_type = false;
			result.is_constant = p_type.is_constant;
		}
		result.is_nullable = result.is_nullable || p_type.is_nullable;
		result.is_substituted_self = p_mark_substitution;
		return result;
	}
	BSParser::DataType result = p_type;
	for (int i = 0; i < result.container_element_types.size(); i++) {
		result.container_element_types.write[i] = _substitute_self_type_parameter_with_bounds(result.container_element_types[i], p_mark_substitution);
	}
	for (int i = 0; i < result.type_arguments.size(); i++) {
		result.set_type_argument(i, _substitute_self_type_parameter_with_bounds(result.type_arguments[i], p_mark_substitution));
	}
	for (int i = 0; i < result.method_parameter_types.size(); i++) {
		result.method_parameter_types.write[i] = _substitute_self_type_parameter_with_bounds(result.method_parameter_types[i], p_mark_substitution);
	}
	for (int i = 0; i < result.method_return_type.size(); i++) {
		result.method_return_type.write[i] = _substitute_self_type_parameter_with_bounds(result.method_return_type[i], p_mark_substitution);
	}
	for (int i = 0; i < result.method_rest_parameter_type.size(); i++) {
		result.method_rest_parameter_type.write[i] = _substitute_self_type_parameter_with_bounds(result.method_rest_parameter_type[i], p_mark_substitution);
	}
	if (result.kind == BSParser::DataType::UNION) {
		Vector<BSParser::DataType> substituted_members;
		for (const BSParser::DataType &member : result.union_members) {
			substituted_members.push_back(_substitute_self_type_parameter_with_bounds(member, p_mark_substitution));
		}
		const bool nullable = result.is_nullable;
		result = BSParser::DataType::make_union(substituted_members);
		result.is_nullable = result.is_nullable || nullable;
	}
	return result;
}

BSParser::DataType _type_handle_represented_type(const BSParser::DataType &p_type) {
	BSParser::DataType represented_type = p_type;
	represented_type.is_type_handle_annotation = false;
	represented_type.is_meta_type = false;
	represented_type.is_pseudo_type = false;
	represented_type.is_constant = false;
	represented_type.is_nullable = false;
	return represented_type;
}

bool _type_handle_source_is_handle(const BSParser::DataType &p_type) {
	return p_type.is_meta_type || p_type.is_type_handle_annotation;
}

bool _datatype_represents_final_class(const BSParser::DataType &p_type) {
	return p_type.kind == BSParser::DataType::CLASS && p_type.class_type != nullptr && p_type.class_type->is_final;
}

bool _datatype_self_bindings_are_final(const BSParser::DataType &p_type) {
	if (_is_self_type_parameter(p_type)) {
		return !p_type.type_parameter_bound.is_empty() && _datatype_represents_final_class(p_type.type_parameter_bound[0]);
	}
	for (const BSParser::DataType &element : p_type.container_element_types) {
		if (!_datatype_self_bindings_are_final(element)) {
			return false;
		}
	}
	for (const BSParser::DataType &argument : p_type.type_arguments) {
		if (!_datatype_self_bindings_are_final(argument)) {
			return false;
		}
	}
	for (const BSParser::DataType &member : p_type.union_members) {
		if (!_datatype_self_bindings_are_final(member)) {
			return false;
		}
	}
	return true;
}

// Foundry _datatype_alpha_equal / _datatype_strict_identity_equal @ c9d5e35: parser equality is
// only the outer gate for alpha comparison. Callable/signature slots and strict identity recurse.
bool _datatype_alpha_equal(const BSParser::DataType &p_a, const BSParser::DataType &p_b) {
	if (!(p_a == p_b) || p_a.has_method_signature != p_b.has_method_signature ||
			p_a.signature_is_async != p_b.signature_is_async || p_a.is_coroutine != p_b.is_coroutine ||
			((p_a.method_info.flags & METHOD_FLAG_VARARG) != (p_b.method_info.flags & METHOD_FLAG_VARARG)) ||
			p_a.container_element_types.size() != p_b.container_element_types.size() ||
			p_a.type_arguments.size() != p_b.type_arguments.size() ||
			p_a.method_parameter_types.size() != p_b.method_parameter_types.size() ||
			p_a.method_return_type.size() != p_b.method_return_type.size() ||
			p_a.method_rest_parameter_type.size() != p_b.method_rest_parameter_type.size() ||
			p_a.type_parameter_bound.size() != p_b.type_parameter_bound.size() ||
			p_a.union_members.size() != p_b.union_members.size()) {
		return false;
	}
	const Vector<BSParser::DataType> *slots_a[] = {
		&p_a.type_parameter_bound,
		&p_a.container_element_types,
		&p_a.type_arguments,
		&p_a.method_parameter_types,
		&p_a.method_return_type,
		&p_a.method_rest_parameter_type,
		&p_a.union_members,
	};
	const Vector<BSParser::DataType> *slots_b[] = {
		&p_b.type_parameter_bound,
		&p_b.container_element_types,
		&p_b.type_arguments,
		&p_b.method_parameter_types,
		&p_b.method_return_type,
		&p_b.method_rest_parameter_type,
		&p_b.union_members,
	};
	for (int group = 0; group < 7; group++) {
		for (int i = 0; i < slots_a[group]->size(); i++) {
			if (!_datatype_alpha_equal((*slots_a[group])[i], (*slots_b[group])[i])) {
				return false;
			}
		}
	}
	return true;
}

bool _datatype_strict_identity_equal(const BSParser::DataType &p_a, const BSParser::DataType &p_b) {
	if (p_a.kind != p_b.kind || p_a.is_nullable != p_b.is_nullable ||
			p_a.is_meta_type != p_b.is_meta_type || p_a.is_type_handle_annotation != p_b.is_type_handle_annotation ||
			p_a.has_method_signature != p_b.has_method_signature || p_a.signature_is_async != p_b.signature_is_async ||
			p_a.is_coroutine != p_b.is_coroutine ||
			((p_a.method_info.flags & METHOD_FLAG_VARARG) != (p_b.method_info.flags & METHOD_FLAG_VARARG)) ||
			p_a.container_element_types.size() != p_b.container_element_types.size() ||
			p_a.type_arguments.size() != p_b.type_arguments.size() ||
			p_a.method_parameter_types.size() != p_b.method_parameter_types.size() ||
			p_a.method_return_type.size() != p_b.method_return_type.size() ||
			p_a.method_rest_parameter_type.size() != p_b.method_rest_parameter_type.size() ||
			p_a.type_parameter_bound.size() != p_b.type_parameter_bound.size() ||
			p_a.union_members.size() != p_b.union_members.size()) {
		return false;
	}

	bool equal = false;
	switch (p_a.kind) {
		case BSParser::DataType::VARIANT:
			equal = true;
			break;
		case BSParser::DataType::BUILTIN:
			equal = p_a.builtin_type == p_b.builtin_type;
			break;
		case BSParser::DataType::NATIVE:
		case BSParser::DataType::ENUM:
			equal = p_a.native_type == p_b.native_type;
			break;
		case BSParser::DataType::SCRIPT:
			equal = p_a.script_type == p_b.script_type;
			break;
		case BSParser::DataType::CLASS:
			equal = p_a.class_type == p_b.class_type ||
					(p_a.class_type != nullptr && p_b.class_type != nullptr && p_a.class_type->fqcn == p_b.class_type->fqcn);
			break;
		case BSParser::DataType::TUPLE:
			equal = p_a.native_type == p_b.native_type && p_a.script_path == p_b.script_path &&
					p_a.tuple_field_names == p_b.tuple_field_names;
			break;
		case BSParser::DataType::UNION:
			equal = p_a.union_members.size() == p_b.union_members.size();
			break;
		case BSParser::DataType::TYPE_PARAMETER:
			equal = p_a.type_parameter_name == p_b.type_parameter_name &&
					p_a.type_parameter_scope == p_b.type_parameter_scope &&
					p_a.type_parameter_index == p_b.type_parameter_index;
			break;
		case BSParser::DataType::RESOLVING:
		case BSParser::DataType::UNRESOLVED:
			break;
	}
	if (!equal) {
		return false;
	}
	const Vector<BSParser::DataType> *slots_a[] = {
		&p_a.type_parameter_bound,
		&p_a.container_element_types,
		&p_a.type_arguments,
		&p_a.method_parameter_types,
		&p_a.method_return_type,
		&p_a.method_rest_parameter_type,
		&p_a.union_members,
	};
	const Vector<BSParser::DataType> *slots_b[] = {
		&p_b.type_parameter_bound,
		&p_b.container_element_types,
		&p_b.type_arguments,
		&p_b.method_parameter_types,
		&p_b.method_return_type,
		&p_b.method_rest_parameter_type,
		&p_b.union_members,
	};
	for (int group = 0; group < 7; group++) {
		for (int i = 0; i < slots_a[group]->size(); i++) {
			if (!_datatype_strict_identity_equal((*slots_a[group])[i], (*slots_b[group])[i])) {
				return false;
			}
		}
	}
	return true;
}

bool _datatype_matches_analyzer_substituted_self(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_actual_type) {
	bool has_container_self = false;
	for (const BSParser::DataType &element : p_expected_type.container_element_types) {
		has_container_self = has_container_self || _datatype_contains_self_type_parameter(element);
	}
	if (!has_container_self) {
		return false;
	}
	const BSParser::DataType expected = _substitute_self_type_parameter_with_bounds(p_expected_type, true);
	if (!_datatype_strict_identity_equal(expected, p_actual_type)) {
		return false;
	}
	const auto markers_match = [&](const auto &self, const BSParser::DataType &a, const BSParser::DataType &b) -> bool {
		if (a.is_substituted_self != b.is_substituted_self ||
				a.container_element_types.size() != b.container_element_types.size() ||
				a.type_arguments.size() != b.type_arguments.size() ||
				a.method_parameter_types.size() != b.method_parameter_types.size() ||
				a.method_return_type.size() != b.method_return_type.size() ||
				a.method_rest_parameter_type.size() != b.method_rest_parameter_type.size() ||
				a.type_parameter_bound.size() != b.type_parameter_bound.size() ||
				a.union_members.size() != b.union_members.size()) {
			return false;
		}
		const Vector<BSParser::DataType> *slots_a[] = {
			&a.type_parameter_bound,
			&a.container_element_types,
			&a.type_arguments,
			&a.method_parameter_types,
			&a.method_return_type,
			&a.method_rest_parameter_type,
			&a.union_members,
		};
		const Vector<BSParser::DataType> *slots_b[] = {
			&b.type_parameter_bound,
			&b.container_element_types,
			&b.type_arguments,
			&b.method_parameter_types,
			&b.method_return_type,
			&b.method_rest_parameter_type,
			&b.union_members,
		};
		for (int group = 0; group < 7; group++) {
			for (int i = 0; i < slots_a[group]->size(); i++) {
				if (!self(self, (*slots_a[group])[i], (*slots_b[group])[i])) {
					return false;
				}
			}
		}
		return true;
	};
	return markers_match(markers_match, expected, p_actual_type);
}

bool _datatype_matches_self_return_contract(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_result_type) {
	if (p_expected_type.is_nullable &&
			p_result_type.kind == BSParser::DataType::BUILTIN &&
			p_result_type.builtin_type == Variant::NIL) {
		return true;
	}
	const BSParser::DataType expected_type = _substitute_self_type_parameter_with_bounds(p_expected_type);
	if (expected_type.is_type_handle_annotation) {
		if (!_type_handle_source_is_handle(p_result_type)) {
			return false;
		}
		const BSParser::DataType result_handle_type = _type_handle_represented_type(p_result_type);
		if (_datatype_strict_identity_equal(_type_handle_represented_type(p_expected_type), result_handle_type)) {
			return true;
		}
		const BSParser::DataType expected_handle_type = _type_handle_represented_type(expected_type);
		if (_datatype_contains_self_type_parameter(p_expected_type) && !_datatype_self_bindings_are_final(p_expected_type)) {
			return false;
		}
		return _datatype_strict_identity_equal(expected_handle_type, result_handle_type);
	}
	if (_datatype_alpha_equal(p_result_type, p_expected_type)) {
		return true;
	}
	if (_datatype_matches_analyzer_substituted_self(p_expected_type, p_result_type)) {
		return true;
	}
	if (p_expected_type.is_nullable) {
		BSParser::DataType non_nullable_expected = p_expected_type;
		non_nullable_expected.is_nullable = false;
		if (_datatype_alpha_equal(p_result_type, non_nullable_expected)) {
			return true;
		}
	}
	if (_datatype_contains_self_type_parameter(p_expected_type) && _datatype_self_bindings_are_final(p_expected_type)) {
		if (_datatype_strict_identity_equal(p_result_type, expected_type)) {
			return true;
		}
		if (expected_type.is_nullable) {
			BSParser::DataType non_nullable_expected = expected_type;
			non_nullable_expected.is_nullable = false;
			return _datatype_strict_identity_equal(p_result_type, non_nullable_expected);
		}
	}
	return false;
}

bool _expression_is_same_reference(const BSParser::ExpressionNode *p_left, const BSParser::ExpressionNode *p_right) {
	if (p_left == nullptr || p_right == nullptr || p_left->type != p_right->type) {
		return false;
	}
	if (p_left->type == BSParser::Node::SELF) {
		return true;
	}
	if (p_left->type == BSParser::Node::IDENTIFIER) {
		const BSParser::IdentifierNode *left = static_cast<const BSParser::IdentifierNode *>(p_left);
		const BSParser::IdentifierNode *right = static_cast<const BSParser::IdentifierNode *>(p_right);
		return left->name == right->name;
	}
	return false;
}

bool _call_argument_is_same_receiver(const BSParser::CallNode *p_call, const BSParser::ExpressionNode *p_argument) {
	if (p_call == nullptr || p_argument == nullptr) {
		return false;
	}
	if (p_call->receiver_is_current_self) {
		return p_argument->type == BSParser::Node::SELF;
	}
	if (p_call->get_callee_type() == BSParser::Node::IDENTIFIER) {
		return p_argument->type == BSParser::Node::SELF;
	}
	if (p_call->get_callee_type() != BSParser::Node::SUBSCRIPT) {
		return false;
	}
	const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
	if (!subscript->is_attribute) {
		if (subscript->base == nullptr) {
			return false;
		}
		if (subscript->base->type == BSParser::Node::IDENTIFIER) {
			return p_argument->type == BSParser::Node::SELF;
		}
		if (subscript->base->type != BSParser::Node::SUBSCRIPT) {
			return false;
		}
		subscript = static_cast<const BSParser::SubscriptNode *>(subscript->base);
	}
	if (!subscript->is_attribute || subscript->base == nullptr) {
		return false;
	}
	if (p_call->is_enum_case_construction) {
		const BSParser::ExpressionNode *union_spelling = subscript->base;
		if (union_spelling->type == BSParser::Node::SUBSCRIPT) {
			const BSParser::SubscriptNode *union_application = static_cast<const BSParser::SubscriptNode *>(union_spelling);
			if (!union_application->is_attribute && union_application->base != nullptr) {
				union_spelling = union_application->base;
			}
		}
		if (union_spelling->type != BSParser::Node::SUBSCRIPT) {
			return false;
		}
		const BSParser::SubscriptNode *union_subscript = static_cast<const BSParser::SubscriptNode *>(union_spelling);
		if (!union_subscript->is_attribute || union_subscript->base == nullptr) {
			return false;
		}
		return _expression_is_same_reference(union_subscript->base, p_argument);
	}
	return _expression_is_same_reference(subscript->base, p_argument);
}

BSParser::DataType _self_contract_comparable_callable_argument(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_argument_type) {
	// Foundry callable arity rewrite for Self-contract comparison. Non-callable values pass through.
	if (!p_expected_type.has_method_signature || !p_argument_type.has_method_signature ||
			(p_argument_type.method_info.flags & METHOD_FLAG_VARARG) == 0) {
		return p_argument_type;
	}
	if ((p_expected_type.method_info.flags & METHOD_FLAG_VARARG) == 0) {
		BSParser::DataType result = p_argument_type;
		result.method_info.flags &= ~METHOD_FLAG_VARARG;
		result.clear_method_rest_parameter_type();
		return result;
	}
	if (p_expected_type.method_rest_parameter_type.size() == 1 && p_argument_type.method_rest_parameter_type.is_empty()) {
		BSParser::DataType result = p_argument_type;
		result.set_method_rest_parameter_type(p_expected_type.method_rest_parameter_type[0]);
		return result;
	}
	return p_argument_type;
}

bool _datatype_matches_self_parameter_contract_exact(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_argument_type) {
	if (p_expected_type == p_argument_type) {
		return true;
	}
	if (_datatype_matches_analyzer_substituted_self(p_expected_type, p_argument_type)) {
		return true;
	}
	if (p_expected_type.is_nullable) {
		BSParser::DataType non_nullable_expected = p_expected_type;
		non_nullable_expected.is_nullable = false;
		if (non_nullable_expected == p_argument_type) {
			return true;
		}
	}
	const BSParser::DataType expected_type = _substitute_self_type_parameter_with_bounds(p_expected_type);
	if (expected_type.is_nullable &&
			p_argument_type.kind == BSParser::DataType::BUILTIN &&
			p_argument_type.builtin_type == Variant::NIL) {
		return true;
	}
	if (!_datatype_self_bindings_are_final(p_expected_type)) {
		return false;
	}
	if (expected_type == p_argument_type) {
		return true;
	}
	if (expected_type.is_nullable) {
		BSParser::DataType non_nullable_expected = expected_type;
		non_nullable_expected.is_nullable = false;
		return non_nullable_expected == p_argument_type;
	}
	return false;
}

bool _datatype_contains_receiver_self_contract(const BSParser::DataType &p_type) {
	if (_is_self_type_parameter(p_type) && p_type.is_receiver_self_contract) {
		return true;
	}
	for (const BSParser::DataType &element : p_type.container_element_types) {
		if (_datatype_contains_receiver_self_contract(element)) {
			return true;
		}
	}
	for (const BSParser::DataType &member : p_type.union_members) {
		if (_datatype_contains_receiver_self_contract(member)) {
			return true;
		}
	}
	return false;
}

bool _call_receiver_is_current_self(const BSParser::CallNode *p_call) {
	if (p_call == nullptr || p_call->get_callee_type() == BSParser::Node::IDENTIFIER || p_call->is_super) {
		return true;
	}
	if (p_call->get_callee_type() != BSParser::Node::SUBSCRIPT) {
		return false;
	}
	const BSParser::SubscriptNode *callee = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
	return callee != nullptr && callee->is_attribute && callee->base != nullptr && callee->base->type == BSParser::Node::SELF;
}

bool _self_parameter_satisfied_by_receiver_identity(const BSParser::DataType &p_expected_type, const BSParser::ExpressionNode *p_argument, const BSParser::CallNode *p_call);

bool _self_parameter_contract_admits_argument_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_argument_type, const BSParser::CallNode *p_call, const BSParser::ExpressionNode *p_argument) {
	if (p_expected_type.kind == BSParser::DataType::UNION) {
		for (const BSParser::DataType &member : p_expected_type.union_members) {
			if (!_datatype_contains_self_type_parameter(member)) {
				continue;
			}
			BSParser::DataType alternative = member;
			alternative.is_nullable = p_expected_type.is_nullable;
			if (_self_parameter_contract_admits_argument_type(alternative, p_argument_type, p_call, p_argument) ||
					_self_parameter_satisfied_by_receiver_identity(alternative, p_argument, p_call)) {
				return true;
			}
		}
		return false;
	}
	if (!_datatype_matches_self_parameter_contract_exact(p_expected_type, p_argument_type)) {
		return false;
	}
	if (_datatype_contains_receiver_self_contract(p_expected_type) &&
			_datatype_contains_self_type_parameter(p_argument_type)) {
		return _call_receiver_is_current_self(p_call);
	}
	if (_is_bare_self_value_parameter(p_argument_type) || p_argument_type.is_substituted_self) {
		if (p_expected_type.is_receiver_self_contract) {
			return p_call == nullptr || p_call->receiver_is_current_self;
		}
	}
	return true;
}

bool _self_parameter_satisfied_by_receiver_identity(const BSParser::DataType &p_expected_type, const BSParser::ExpressionNode *p_argument, const BSParser::CallNode *p_call) {
	if (p_call == nullptr || p_argument == nullptr) {
		return false;
	}
	if (p_expected_type.kind == BSParser::DataType::UNION) {
		for (const BSParser::DataType &member : p_expected_type.union_members) {
			if (!_datatype_contains_self_type_parameter(member)) {
				continue;
			}
			BSParser::DataType alternative = member;
			alternative.is_nullable = p_expected_type.is_nullable;
			if (_self_parameter_satisfied_by_receiver_identity(alternative, p_argument, p_call)) {
				return true;
			}
		}
		return false;
	}
	if (_is_bare_self_value_parameter(p_expected_type)) {
		return p_expected_type.is_receiver_self_contract && _call_argument_is_same_receiver(p_call, p_argument);
	}
	if (p_expected_type.kind != BSParser::DataType::TUPLE || p_expected_type.tuple_name != StringName() ||
			p_argument->type != BSParser::Node::TUPLE_LITERAL) {
		return false;
	}
	const BSParser::TupleLiteralNode *literal = static_cast<const BSParser::TupleLiteralNode *>(p_argument);
	if (literal->elements.size() != p_expected_type.container_element_types.size()) {
		return false;
	}
	for (int i = 0; i < literal->elements.size(); i++) {
		const BSParser::DataType &expected_element = p_expected_type.container_element_types[i];
		BSParser::ExpressionNode *element = literal->elements[i];
		if (element == nullptr) {
			return false;
		}
		const BSParser::DataType element_type = element->get_datatype();
		if (_datatype_contains_self_type_parameter(expected_element)) {
			if (_self_parameter_contract_admits_argument_type(expected_element, element_type, p_call, element)) {
				continue;
			}
			if (!_self_parameter_satisfied_by_receiver_identity(expected_element, element, p_call)) {
				return false;
			}
			continue;
		}
		if (expected_element.is_hard_type() && expected_element.is_variant()) {
			continue;
		}
		BSTypeCompatibility::Options options;
		options.allow_implicit_conversion = true;
		if (!element_type.is_hard_type() || !BSTypeCompatibility::check(expected_element, element_type, options).compatible) {
			return false;
		}
		if (expected_element.kind == BSParser::DataType::BUILTIN &&
				element_type.kind == BSParser::DataType::BUILTIN &&
				expected_element.builtin_type != element_type.builtin_type) {
			return false;
		}
	}
	return true;
}

bool _self_contract_admits_value_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_value_type, BSAnalyzer::SelfContractKind p_kind, const BSParser::ExpressionNode *p_value_source, BSParser::DataType *r_matched_value);

// Foundry self_free_union_members_admit_value @ c9d5e35: alternatives that name no Self are answered
// by ordinary compatibility (implicit conversion first; exact builtin match when conversion widened).
bool _self_free_union_members_admit_value(
		const Vector<BSParser::DataType> &p_members,
		bool p_nullable,
		const BSParser::DataType &p_value_type,
		const BSParser::ExpressionNode *p_value_source) {
	(void)p_value_source;
	if (p_members.is_empty()) {
		return false;
	}
	BSParser::DataType self_free_union = BSParser::DataType::make_union(p_members);
	if (!self_free_union.is_set()) {
		return false;
	}
	self_free_union.is_nullable = self_free_union.is_nullable || p_nullable;
	BSTypeCompatibility::Options allow_conversion;
	allow_conversion.allow_implicit_conversion = true;
	if (!BSTypeCompatibility::check(self_free_union, p_value_type, allow_conversion).compatible) {
		return false;
	}
	if (self_free_union.kind == BSParser::DataType::UNION ||
			self_free_union.kind != BSParser::DataType::BUILTIN ||
			p_value_type.kind != BSParser::DataType::BUILTIN ||
			self_free_union.builtin_type == p_value_type.builtin_type) {
		return true;
	}
	BSTypeCompatibility::Options exact;
	exact.allow_implicit_conversion = false;
	return BSTypeCompatibility::check(self_free_union, p_value_type, exact).compatible;
}

bool _self_contract_union_admits_value_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_value_type, BSAnalyzer::SelfContractKind p_kind, const BSParser::CallNode *p_call, const BSParser::ExpressionNode *p_value_source, BSParser::DataType *r_matched_value) {
	if (p_value_type.kind == BSParser::DataType::UNION) {
		for (const BSParser::DataType &value_member : p_value_type.union_members) {
			BSParser::DataType value_alternative = value_member;
			value_alternative.is_nullable = p_value_type.is_nullable;
			if (!_self_contract_union_admits_value_type(p_expected_type, value_alternative, p_kind, p_call, p_value_source, nullptr)) {
				return false;
			}
		}
		if (r_matched_value != nullptr) {
			*r_matched_value = p_value_type;
		}
		return true;
	}

	Vector<BSParser::DataType> self_free_members;
	for (const BSParser::DataType &member : p_expected_type.union_members) {
		if (!_datatype_contains_self_type_parameter(member)) {
			self_free_members.push_back(member);
			continue;
		}
		BSParser::DataType alternative = member;
		alternative.is_nullable = p_expected_type.is_nullable;
		const bool admitted = p_kind == BSAnalyzer::SelfContractKind::PARAMETER
				? _self_parameter_contract_admits_argument_type(alternative, p_value_type, p_call, p_value_source)
				: _self_contract_admits_value_type(alternative, p_value_type, p_kind, p_value_source, r_matched_value);
		if (admitted) {
			if (r_matched_value != nullptr) {
				*r_matched_value = p_value_type;
			}
			return true;
		}
	}
	if (!_self_free_union_members_admit_value(
				self_free_members, p_expected_type.is_nullable, p_value_type, p_value_source)) {
		return false;
	}
	if (r_matched_value != nullptr) {
		*r_matched_value = p_value_type;
	}
	return true;
}

bool _self_contract_admits_value_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_value_type, BSAnalyzer::SelfContractKind p_kind, const BSParser::ExpressionNode *p_value_source, BSParser::DataType *r_matched_value) {
	(void)p_value_source;
	if (p_expected_type.kind == BSParser::DataType::UNION) {
		return _self_contract_union_admits_value_type(p_expected_type, p_value_type, p_kind, nullptr, p_value_source, r_matched_value);
	}
	BSParser::DataType matched_value = _self_contract_comparable_callable_argument(p_expected_type, p_value_type);
	const auto matches_exactly = [&](const BSParser::DataType &p_candidate) {
		return p_kind == BSAnalyzer::SelfContractKind::PARAMETER
				? _datatype_matches_self_parameter_contract_exact(p_expected_type, p_candidate)
				: _datatype_matches_self_return_contract(p_expected_type, p_candidate);
	};
	if (!matches_exactly(matched_value)) {
		return false;
	}
	if (r_matched_value != nullptr) {
		*r_matched_value = matched_value;
	}
	return true;
}

} // namespace

#ifdef DEBUG_ENABLED
Dictionary BSAnalyzer::debug_self_identity_controls() {
	const auto builtin = [](Variant::Type p_type) {
		BSParser::DataType result;
		result.kind = BSParser::DataType::BUILTIN;
		result.builtin_type = p_type;
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	};
	const BSParser::DataType int_type = builtin(Variant::INT);
	const BSParser::DataType string_type = builtin(Variant::STRING);
	const BSParser::DataType void_type = builtin(Variant::NIL);

	BSParser::DataType callable;
	callable.kind = BSParser::DataType::BUILTIN;
	callable.builtin_type = Variant::CALLABLE;
	callable.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	callable.has_method_signature = true;
	callable.method_parameter_types.push_back(int_type);
	callable.method_return_type.push_back(void_type);

	BSParser::DataType fixed_mismatch = callable;
	fixed_mismatch.method_parameter_types.write[0] = string_type;
	BSParser::DataType return_mismatch = callable;
	return_mismatch.method_return_type.write[0] = int_type;
	BSParser::DataType async_mismatch = callable;
	async_mismatch.signature_is_async = true;
	BSParser::DataType rest_mismatch = callable;
	rest_mismatch.method_info.flags |= METHOD_FLAG_VARARG;
	BSParser::DataType rest_array = builtin(Variant::ARRAY);
	rest_array.container_element_types.push_back(int_type);
	rest_mismatch.method_rest_parameter_type.push_back(rest_array);

	BSParser::DataType nested_a = builtin(Variant::ARRAY);
	nested_a.container_element_types.push_back(callable);
	BSParser::DataType nested_b = builtin(Variant::ARRAY);
	nested_b.container_element_types.push_back(return_mismatch);

	BSParser::DataType union_a;
	union_a.kind = BSParser::DataType::UNION;
	union_a.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	union_a.union_members.push_back(callable);
	union_a.union_members.push_back(string_type);
	BSParser::DataType union_b = union_a;
	union_b.union_members.write[0] = fixed_mismatch;

	BSParser::DataType self_type;
	self_type.kind = BSParser::DataType::TYPE_PARAMETER;
	self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.type_parameter_name = SNAME("@Self");
	self_type.type_parameter_scope = BSParser::DataType::TYPE_PARAMETER_CLASS;
	self_type.type_parameter_bound.push_back(int_type);
	BSParser::DataType self_callable = callable;
	self_callable.method_parameter_types.write[0] = self_type;
	self_callable.method_return_type.write[0] = self_type;
	self_callable.method_info.flags |= METHOD_FLAG_VARARG;
	BSParser::DataType self_rest = builtin(Variant::ARRAY);
	self_rest.container_element_types.push_back(self_type);
	self_callable.method_rest_parameter_type.push_back(self_rest);
	BSParser::DataType self_container = builtin(Variant::ARRAY);
	self_container.container_element_types.push_back(self_callable);
	const BSParser::DataType marked = _substitute_self_type_parameter_with_bounds(self_container, true);
	BSParser::DataType missing_fixed_marker = marked;
	missing_fixed_marker.container_element_types.write[0].method_parameter_types.write[0].is_substituted_self = false;
	BSParser::DataType missing_return_marker = marked;
	missing_return_marker.container_element_types.write[0].method_return_type.write[0].is_substituted_self = false;
	BSParser::DataType missing_rest_marker = marked;
	missing_rest_marker.container_element_types.write[0].method_rest_parameter_type.write[0].container_element_types.write[0].is_substituted_self = false;

	BSParser::DataType undetected;
	undetected.kind = BSParser::DataType::VARIANT;
	undetected.type_source = BSParser::DataType::UNDETECTED;

	Dictionary result;
	result["alpha_fixed_slot"] = !_datatype_alpha_equal(callable, fixed_mismatch);
	result["strict_fixed_slot"] = !_datatype_strict_identity_equal(callable, fixed_mismatch);
	result["strict_return_slot"] = !_datatype_strict_identity_equal(callable, return_mismatch);
	result["strict_async"] = !_datatype_strict_identity_equal(callable, async_mismatch);
	result["strict_rest"] = !_datatype_strict_identity_equal(callable, rest_mismatch);
	result["strict_nested"] = !_datatype_strict_identity_equal(nested_a, nested_b);
	result["strict_union"] = !_datatype_strict_identity_equal(union_a, union_b);
	result["strict_ignores_parser_wildcard"] = !_datatype_strict_identity_equal(undetected, int_type);
	result["markers_match"] = _datatype_matches_analyzer_substituted_self(self_container, marked);
	result["markers_fixed_slot"] = !_datatype_matches_analyzer_substituted_self(self_container, missing_fixed_marker);
	result["markers_return_slot"] = !_datatype_matches_analyzer_substituted_self(self_container, missing_return_marker);
	result["markers_rest_slot"] = !_datatype_matches_analyzer_substituted_self(self_container, missing_rest_marker);
	return result;
}
#endif

void BSAnalyzer::reduce_call_enum_case_construction(BSParser::CallNode *p_call, const BSParser::DataType &p_enum_meta_type) {
	// Foundry reduce_call_enum_case_construction @ c9d5e35 (SelfFieldLeg + self-ref completion):
	// spelling-aware `@Self` payload admission + complete_self_referential_enum_type on fields.
	// open_union_members_collapse / full open-schema alternative admission remain #60 residuals.
	if (p_call == nullptr) {
		return;
	}
	call_site_validation.reject_named_call_arguments(p_call);

	const StringName case_name = p_call->function_name != StringName()
			? p_call->function_name
			: (p_call->callee != nullptr && p_call->callee->type == BSParser::Node::SUBSCRIPT &&
									  static_cast<BSParser::SubscriptNode *>(p_call->callee)->attribute != nullptr
							  ? static_cast<BSParser::SubscriptNode *>(p_call->callee)->attribute->name
							  : StringName());
	const BSParser::DataType::EnumCasePayload *payload = p_enum_meta_type.get_enum_case_payload(case_name);
	ERR_FAIL_NULL(payload);

	const int64_t *tag = p_enum_meta_type.enum_values.getptr(case_name);
	p_call->is_enum_case_construction = true;
	p_call->enum_case_tag = tag != nullptr ? *tag : 0;
	p_call->function_name = case_name;

	BSParser::ClassNode *declaring_class = p_enum_meta_type.class_type;
	const BSParser::ExpressionNode *union_expression = nullptr;
	if (p_call->callee != nullptr && p_call->callee->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *callee_subscript = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
		if (callee_subscript->is_attribute) {
			union_expression = callee_subscript->base;
		}
	}
	if (union_expression != nullptr && union_expression->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *union_application = static_cast<const BSParser::SubscriptNode *>(union_expression);
		if (!union_application->is_attribute && union_application->base != nullptr) {
			union_expression = union_application->base;
		}
	}
	const BSParser::ExpressionNode *union_base_expression = nullptr;
	if (union_expression != nullptr && union_expression->type == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *union_subscript = static_cast<const BSParser::SubscriptNode *>(union_expression);
		if (union_subscript->is_attribute) {
			union_base_expression = union_subscript->base;
		}
	}

	const bool static_context = current_function != nullptr && current_function->is_static;
	bool frame_self_is_declaring_instance = false;
	if (!static_context && declaring_class != nullptr) {
		for (const BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->base_type.class_type) {
			if (scope == declaring_class || scope->resolved_traits.has(declaring_class)) {
				frame_self_is_declaring_instance = true;
				break;
			}
		}
	}

	enum class SelfFieldLeg {
		FRAME_RECEIVER,
		BASE_RECEIVER,
		EXACT_HANDLE,
		EXACT_DECLARING,
		LITERAL_SELF,
	};
	SelfFieldLeg self_field_leg = SelfFieldLeg::EXACT_DECLARING;
	BSParser::DataType union_base_type;
	if (union_base_expression != nullptr && union_base_expression->type == BSParser::Node::SELF) {
		self_field_leg = SelfFieldLeg::FRAME_RECEIVER;
	} else if (union_base_expression != nullptr) {
		union_base_type = union_base_expression->get_datatype();
		if (union_base_type.is_set() && (union_base_type.is_meta_type || union_base_type.is_type_handle_annotation) &&
				_is_self_type_parameter(union_base_type)) {
			self_field_leg = !static_context && current_class != nullptr
					? SelfFieldLeg::FRAME_RECEIVER
					: SelfFieldLeg::LITERAL_SELF;
		} else if (union_base_type.is_set() && (union_base_type.is_meta_type || union_base_type.is_type_handle_annotation)) {
			self_field_leg = SelfFieldLeg::EXACT_HANDLE;
		} else if (union_base_type.is_set() &&
				(union_base_type.kind == BSParser::DataType::CLASS ||
						union_base_type.kind == BSParser::DataType::TYPE_PARAMETER)) {
			bool names_a_handle = false;
			const BSParser::DataType *bound_step = &union_base_type;
			while (bound_step->kind == BSParser::DataType::TYPE_PARAMETER && !bound_step->type_parameter_bound.is_empty()) {
				const BSParser::DataType &bound = bound_step->type_parameter_bound[0];
				if (_type_handle_source_is_handle(bound)) {
					names_a_handle = true;
					break;
				}
				if (bound.kind != BSParser::DataType::TYPE_PARAMETER) {
					break;
				}
				bound_step = &bound;
			}
			if (!names_a_handle) {
				self_field_leg = SelfFieldLeg::BASE_RECEIVER;
			}
		}
	} else if (frame_self_is_declaring_instance) {
		self_field_leg = SelfFieldLeg::FRAME_RECEIVER;
	}
	if (self_field_leg == SelfFieldLeg::FRAME_RECEIVER) {
		p_call->receiver_is_current_self = true;
	}

	const auto payload_field_type_for_spelling = [&](const BSParser::DataType &p_field_type) -> BSParser::DataType {
		if (!_datatype_contains_self_type_parameter(p_field_type)) {
			return p_field_type;
		}
		switch (self_field_leg) {
			case SelfFieldLeg::FRAME_RECEIVER: {
				BSParser::DataType receiver_self = _self_type_parameter_from_bound(_self_type_for_class(current_class));
				receiver_self.is_receiver_self_contract = true;
				return _substitute_self_type_parameter(p_field_type, receiver_self);
			}
			case SelfFieldLeg::BASE_RECEIVER: {
				BSParser::DataType receiver_self = _self_type_parameter_from_bound(union_base_type);
				receiver_self.is_receiver_self_contract = true;
				return _substitute_self_type_parameter(p_field_type, receiver_self);
			}
			case SelfFieldLeg::EXACT_HANDLE: {
				BSParser::DataType represented_type = _type_handle_represented_type(union_base_type);
				if (!represented_type.is_set()) {
					return p_field_type;
				}
				return _substitute_self_type_parameter(p_field_type, represented_type);
			}
			case SelfFieldLeg::LITERAL_SELF: {
				if (current_class == nullptr) {
					return p_field_type;
				}
				BSParser::DataType frame_self = _self_type_parameter_for_class(current_class);
				return _substitute_self_type_parameter(p_field_type, frame_self);
			}
			case SelfFieldLeg::EXACT_DECLARING:
				break;
		}
		if (declaring_class == nullptr) {
			return _substitute_self_type_parameter_with_bounds(p_field_type);
		}
		BSParser::DataType declaring_self = _self_type_for_class(declaring_class);
		return _substitute_self_type_parameter(p_field_type, declaring_self);
	};

	// Foundry open-schema + type-argument bindings @ c9d5e35. Non-generic unions leave
	// bindings empty; generic fixtures remain M5-blocked at resolve_enum_values, but the
	// specialize path is wired for when open payloads + arguments are present.
	const BSParser::EnumNode *declaration = nullptr;
	if (declaring_class != nullptr) {
		if (declaring_class->is_enum_file) {
			declaration = declaring_class->enum_file_decl;
		} else if (declaring_class->has_member(p_enum_meta_type.enum_type)) {
			const BSParser::ClassNode::Member member = declaring_class->get_member(p_enum_meta_type.enum_type);
			if (member.type == BSParser::ClassNode::Member::ENUM) {
				declaration = member.m_enum;
			}
		}
	}
	HashMap<StringName, BSParser::DataType> type_argument_bindings;
	BSParser::DataType open_declaration_type;
	if (declaration != nullptr && !declaration->type_parameters.is_empty()) {
		type_argument_bindings = enum_type_argument_bindings(declaration, p_enum_meta_type.type_arguments);
		if (!type_argument_bindings.is_empty()) {
			open_declaration_type = declaration->get_datatype();
		}
	}
	const auto open_payload_field = [&](const StringName &p_case_name, int p_index) -> const BSParser::DataType * {
		if (!open_declaration_type.is_set()) {
			return nullptr;
		}
		const BSParser::DataType::EnumCasePayload *open_payload = open_declaration_type.get_enum_case_payload(p_case_name);
		if (open_payload == nullptr || p_index >= open_payload->field_types.size()) {
			return nullptr;
		}
		return &open_payload->field_types[p_index];
	};

	// Foundry checked_payload_field_type @ c9d5e35: complete recursive shells after spelling /
	// type-argument transform. open_union_members_collapse remains an explicit #60 residual.
	const auto checked_payload_field_type = [&](int p_index, const BSParser::DataType &p_specialized_field) -> BSParser::DataType {
		const BSParser::DataType *open_field = open_payload_field(case_name, p_index);
		if (open_field == nullptr) {
			return payload_field_type_for_spelling(complete_self_referential_enum_type(p_specialized_field));
		}
		return complete_self_referential_enum_type(
				BSParser::DataType::substitute(payload_field_type_for_spelling(*open_field), type_argument_bindings));
	};

	BSParser::DataType case_value_type = type_from_metatype(p_enum_meta_type);
	const int expected_count = payload->field_types.size();
	if (p_call->arguments.size() != expected_count) {
		push_error(vformat(R"*(Enum case "%s.%s" expects %d argument(s), but %d were given.)*",
						   p_enum_meta_type.enum_type, case_name, expected_count, p_call->arguments.size()),
				p_call);
		p_call->set_datatype(case_value_type);
		return;
	}

	bool payload_is_bakeable = true;
	for (int i = 0; i < expected_count; i++) {
		const BSParser::DataType field_type = checked_payload_field_type(i, payload->field_types[i]);
		BSParser::ExpressionNode *argument = p_call->arguments[i];
		if (argument == nullptr) {
			payload_is_bakeable = false;
			continue;
		}
		// Nested shorthand in payload position takes its union from the field type.
		resolve_contextual_enum_case(argument, field_type);
		update_container_literal_element_types(argument, field_type);
		const BSParser::DataType argument_type = argument->get_datatype();
		if (!argument_type.is_set()) {
			payload_is_bakeable = false;
			continue;
		}
		if (_datatype_contains_self_type_parameter(field_type)) {
			if (!_self_parameter_contract_admits_argument_type(field_type, argument_type, p_call, argument) &&
					!_self_parameter_satisfied_by_receiver_identity(field_type, argument, p_call)) {
				push_error(vformat(R"*(Invalid argument %d for enum case "%s.%s": should be "%s" but is "%s".)*",
								   i + 1, p_enum_meta_type.enum_type, case_name, field_type.to_string(), argument_type.to_string()) +
								BSParser::DataType::same_rendered_name_clause(field_type, "payload field's type", argument_type, "argument"),
						argument);
				payload_is_bakeable = false;
				continue;
			}
			if (!field_type.is_variant() && (argument_type.is_variant() || !argument_type.is_hard_type())) {
				mark_node_unsafe(p_call);
			}
		} else {
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = true;
			options.strict_dynamic = strict_dynamic_checks;
			options.strict_null = strict_null_checks;
			if (argument->is_constant) {
				options.constant_source_value = &argument->reduced_value;
			}
			if (!BSTypeCompatibility::check(field_type, argument_type, options).compatible) {
				push_error(vformat(R"*(Invalid argument %d for enum case "%s.%s": should be "%s" but is "%s".)*",
								   i + 1, p_enum_meta_type.enum_type, case_name, field_type.to_string(), argument_type.to_string()) +
								BSParser::DataType::same_rendered_name_clause(field_type, "payload field's type", argument_type, "argument"),
						argument);
				payload_is_bakeable = false;
				continue;
			}
		}
		if (!argument->is_constant) {
			payload_is_bakeable = false;
		}
	}

	if (payload_is_bakeable) {
		for (int i = 0; i < expected_count && payload_is_bakeable; i++) {
			const BSParser::ExpressionNode *argument = p_call->arguments[i];
			const Variant &value = argument->reduced_value;
			switch (value.get_type()) {
				case Variant::OBJECT:
					payload_is_bakeable = false;
					break;
				case Variant::ARRAY:
					payload_is_bakeable = static_cast<Array>(value).is_read_only();
					break;
				case Variant::DICTIONARY:
					payload_is_bakeable = static_cast<Dictionary>(value).is_read_only();
					break;
				default:
					break;
			}
			const BSParser::DataType field_type = complete_self_referential_enum_type(payload->field_types[i]);
			if (field_type.kind == BSParser::DataType::BUILTIN && field_type.has_container_element_types()) {
				payload_is_bakeable = false;
			} else if (field_type.kind != BSParser::DataType::VARIANT && field_type.kind != BSParser::DataType::ENUM &&
					field_type.kind != BSParser::DataType::BUILTIN) {
				payload_is_bakeable = false;
			}
		}
	}
	if (payload_is_bakeable) {
		Array case_value;
		case_value.push_back(p_call->enum_case_tag);
		for (int i = 0; i < expected_count; i++) {
			case_value.push_back(p_call->arguments[i]->reduced_value);
		}
		case_value.make_read_only();
		p_call->is_constant = true;
		p_call->reduced_value = case_value;
	}

	p_call->set_datatype(case_value_type);
}

bool BSAnalyzer::find_named_tuple_meta_type(const BSParser::DataType &p_base_type, bool p_is_self, const StringName &p_name,
		const BSParser::Node *p_source, BSParser::DataType &r_tuple_meta_type) {
	if (p_name == StringName()) {
		return false;
	}

	BSParser::DataType receiver_type = p_base_type;
	if (_is_self_type_parameter(receiver_type) && !receiver_type.type_parameter_bound.is_empty()) {
		receiver_type = receiver_type.type_parameter_bound[0];
		receiver_type.is_meta_type = p_base_type.is_meta_type;
	}
	BSParser::ClassNode *start = p_is_self ? current_class : receiver_type.class_type;
	if (start == nullptr || (!p_is_self && receiver_type.kind != BSParser::DataType::CLASS)) {
		return false;
	}

	const auto tuple_type_for_spelling = [&](const BSParser::DataType &p_tuple_type,
												 BSParser::ClassNode *p_declaring_class) -> BSParser::DataType {
		if (!_datatype_contains_self_type_parameter(p_tuple_type)) {
			return p_tuple_type;
		}
		if (!p_is_self) {
			if (receiver_type.is_meta_type) {
				BSParser::DataType declaring_value = receiver_type;
				declaring_value.is_meta_type = false;
				return _substitute_self_type_parameter(p_tuple_type, declaring_value);
			}
			BSParser::DataType receiver_self = _self_type_parameter_from_bound(receiver_type);
			receiver_self.is_receiver_self_contract = true;
			return _substitute_self_type_parameter(p_tuple_type, receiver_self);
		}

		bool frame_is_declaring_instance = false;
		if (current_function == nullptr || !current_function->is_static) {
			HashSet<const BSParser::ClassNode *> visited;
			for (BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->base_type.class_type) {
				if (visited.has(scope)) {
					break;
				}
				visited.insert(scope);
				if (scope == p_declaring_class) {
					frame_is_declaring_instance = true;
					break;
				}
			}
		}
		if (frame_is_declaring_instance) {
			BSParser::DataType receiver_self = _self_type_parameter_from_bound(_self_type_for_class(current_class));
			receiver_self.is_receiver_self_contract = true;
			return _substitute_self_type_parameter(p_tuple_type, receiver_self);
		}
		return _substitute_self_type_parameter(p_tuple_type, _self_type_for_class(p_declaring_class));
	};

	for (BSParser::ClassNode *lexical = start; lexical != nullptr; lexical = p_is_self ? lexical->outer : nullptr) {
		HashSet<const BSParser::ClassNode *> visited;
		for (BSParser::ClassNode *scope = lexical; scope != nullptr; scope = scope->base_type.class_type) {
			if (visited.has(scope)) {
				break;
			}
			visited.insert(scope);
			if (!scope->has_member(p_name)) {
				continue;
			}
			resolve_class_member(scope, p_name, p_source);
			const BSParser::ClassNode::Member member = scope->get_member(p_name);
			if (member.type != BSParser::ClassNode::Member::TUPLE || member.m_tuple == nullptr) {
				return false;
			}
			r_tuple_meta_type = tuple_type_for_spelling(member.m_tuple->get_datatype(), scope);
			return r_tuple_meta_type.kind == BSParser::DataType::TUPLE && r_tuple_meta_type.is_meta_type;
		}
	}
	return false;
}

bool BSAnalyzer::find_named_tuple_meta_type(const StringName &p_name, BSParser::DataType &r_tuple_meta_type) {
	BSParser::DataType receiver_type = current_class != nullptr ? current_class->get_datatype() : BSParser::DataType();
	receiver_type.is_meta_type = false;
	return find_named_tuple_meta_type(receiver_type, true, p_name, nullptr, r_tuple_meta_type);
}

void BSAnalyzer::reduce_call_tuple_construction(BSParser::CallNode *p_call, const BSParser::DataType &p_tuple_meta_type) {
	call_site_validation.reject_named_call_arguments(p_call);
	p_call->is_tuple_construction = true;
	BSParser::DataType tuple_type = type_from_metatype(p_tuple_meta_type);
	tuple_type.is_read_only = true;
	const int expected_count = tuple_type.container_element_types.size();
	if (p_call->arguments.size() != expected_count) {
		push_error(vformat(R"*(Tuple "%s" expects %d argument(s), but %d were given.)*",
						   tuple_type.to_string(), expected_count, p_call->arguments.size()),
				p_call);
		p_call->set_datatype(tuple_type);
		return;
	}

	bool all_constant = true;
	Array values;
	for (int i = 0; i < expected_count; i++) {
		const BSParser::DataType field_type = tuple_type.get_container_element_type(i);
		BSParser::ExpressionNode *argument = p_call->arguments[i];
		qualify_contextual_enum_case_consumer(argument, field_type);
		const BSParser::DataType argument_type = argument->get_datatype();
		bool compatible = true;
		if (_datatype_contains_self_type_parameter(field_type)) {
			compatible = _self_parameter_contract_admits_argument_type(field_type, argument_type, p_call, argument) ||
					_self_parameter_satisfied_by_receiver_identity(field_type, argument, p_call);
		} else {
			BSTypeCompatibility::Options options;
			options.allow_implicit_conversion = true;
			options.strict_dynamic = strict_dynamic_checks;
			options.strict_null = strict_null_checks;
			if (argument->is_constant) {
				options.constant_source_value = &argument->reduced_value;
			}
			compatible = BSTypeCompatibility::check(field_type, argument_type, options).compatible;
		}
		if (!compatible) {
			push_error(vformat(R"*(Invalid argument %d for tuple "%s": should be "%s" but is "%s".)*",
							   i + 1, tuple_type.to_string(), field_type.to_string(), argument_type.to_string()) +
							BSParser::DataType::same_rendered_name_clause(field_type, "tuple field's type", argument_type, "argument"),
					argument);
		}
		if (argument == nullptr || !argument->is_constant) {
			all_constant = false;
		} else {
			values.push_back(argument->reduced_value);
		}
	}
	if (all_constant) {
		values.make_read_only();
		p_call->is_constant = true;
		p_call->reduced_value = values;
		tuple_type.is_constant = true;
	}
	p_call->set_datatype(tuple_type);
}

bool BSAnalyzer::datatype_contains_self_type_parameter(const BSParser::DataType &p_type) const {
	return _datatype_contains_self_type_parameter(p_type);
}

bool BSAnalyzer::datatype_strict_identity_equal(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_actual_type) const {
	return _datatype_strict_identity_equal(p_expected_type, p_actual_type);
}

bool BSAnalyzer::self_contract_admits_value_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_value_type, SelfContractKind p_kind, const BSParser::ExpressionNode *p_value_source) const {
	return _self_contract_admits_value_type(p_expected_type, p_value_type, p_kind, p_value_source, nullptr);
}

bool BSAnalyzer::gradual_destination_is_undecidable(const BSParser::DataType &p_destination) const {
	// Foundry gradual_destination_is_undecidable @ c9d5e35.
	BSTypeCompatibility::Options options;
	const bool static_context = current_function != nullptr && current_function->is_static;
	options.receiver_is_available = !static_context;
	return BSTypeCompatibility::destination_is_undecidable_type_parameter(p_destination, options);
}

bool BSAnalyzer::self_contract_admits_gradual_value(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_value_type) const {
	// Foundry self_contract_admits_gradual_value @ c9d5e35: UNION-only booking of a runtime check.
	// Refuses the same two promises ordinary validation refuses: strict_dynamic Variant, and an
	// undecidable (erased / receiver-missing) destination.
	if (p_expected_type.kind != BSParser::DataType::UNION) {
		return false;
	}
	if (p_value_type.is_variant() && strict_dynamic_checks) {
		return false;
	}
	if (gradual_destination_is_undecidable(p_expected_type)) {
		return false;
	}
	return true;
}

bool BSAnalyzer::self_parameter_contract_admits_argument_type(const BSParser::DataType &p_expected_type, const BSParser::DataType &p_argument_type, const BSParser::CallNode *p_call, const BSParser::ExpressionNode *p_argument) const {
	return _self_parameter_contract_admits_argument_type(p_expected_type, p_argument_type, p_call, p_argument);
}

bool BSAnalyzer::self_parameter_satisfied_by_receiver_identity(const BSParser::DataType &p_expected_type, const BSParser::ExpressionNode *p_argument, const BSParser::CallNode *p_call) const {
	return _self_parameter_satisfied_by_receiver_identity(p_expected_type, p_argument, p_call);
}

String BSAnalyzer::self_parameter_receiver_identity_clause(const BSParser::DataType &p_expected_type,
		const BSParser::DataType &p_argument_type, const BSParser::CallNode *p_call) const {
	if (p_call == nullptr || !_datatype_contains_self_type_parameter(p_expected_type) ||
			!_datatype_contains_self_type_parameter(p_argument_type)) {
		return String();
	}
	bool receiver_is_current = p_call->get_callee_type() == BSParser::Node::IDENTIFIER;
	if (p_call->get_callee_type() == BSParser::Node::SUBSCRIPT) {
		const BSParser::SubscriptNode *callee = static_cast<const BSParser::SubscriptNode *>(p_call->callee);
		receiver_is_current = callee != nullptr && callee->is_attribute && callee->base != nullptr &&
				callee->base->type == BSParser::Node::SELF;
	}
	if (receiver_is_current || p_call->is_super) {
		return String();
	}

	const auto element_label = [](const BSParser::DataType &type, int index) -> String {
		if (type.kind == BSParser::DataType::TUPLE) {
			if (index < type.tuple_field_names.size() && type.tuple_field_names[index] != StringName()) {
				return vformat(R"*(element "%s")*", type.tuple_field_names[index]);
			}
			return vformat("element %d", index + 1);
		}
		return type.container_element_types.size() > 1 ? vformat("element type %d", index + 1) : String("the element type");
	};
	const auto find_slot = [&](const auto &self, const BSParser::DataType &expected, const BSParser::DataType &actual) -> String {
		if (expected.container_element_types.size() != actual.container_element_types.size()) {
			return String();
		}
		for (int i = 0; i < expected.container_element_types.size(); i++) {
			const BSParser::DataType &expected_element = expected.container_element_types[i];
			const BSParser::DataType &actual_element = actual.container_element_types[i];
			if (!_datatype_contains_self_type_parameter(expected_element) || !_datatype_contains_self_type_parameter(actual_element)) {
				continue;
			}
			const String label = element_label(expected, i);
			if (_datatype_strict_identity_equal(expected_element, actual_element)) {
				return label;
			}
			const String inner = self(self, expected_element, actual_element);
			if (!inner.is_empty()) {
				return inner + String(" of ") + label;
			}
		}
		return String();
	};
	const String slot = _datatype_strict_identity_equal(p_expected_type, p_argument_type)
			? String()
			: find_slot(find_slot, p_expected_type, p_argument_type);
	return vformat(R"*( The parameter's "Self"%s is resolved against the receiver expression; the argument is relative to the calling frame's receiver.)*",
			slot.is_empty() ? String() : " at " + slot);
}

bool BSAnalyzer::resolve_contextual_enum_case(BSParser::ExpressionNode *p_expression, const BSParser::DataType &p_expected_type) {
	// Foundry resolve_contextual_enum_case @ c9d5e35: qualify expression-position `.Case`.
	if (p_expression != nullptr && p_expression->type == BSParser::Node::TERNARY_OPERATOR) {
		BSParser::TernaryOpNode *ternary = static_cast<BSParser::TernaryOpNode *>(p_expression);
		const bool true_awaits = contextual_enum_case_awaits_expected_type(ternary->true_expr);
		const bool false_awaits = contextual_enum_case_awaits_expected_type(ternary->false_expr);
		if (!true_awaits && !false_awaits) {
			return false;
		}
		if (true_awaits) {
			resolve_contextual_enum_case(ternary->true_expr, p_expected_type);
		}
		if (false_awaits) {
			resolve_contextual_enum_case(ternary->false_expr, p_expected_type);
		}
		return true;
	}

	BSParser::CallNode *call = nullptr;
	BSParser::SubscriptNode *reference = _contextual_enum_case_reference(p_expression, &call);
	if (reference == nullptr) {
		return false;
	}

	if (resolved_contextual_enum_cases.has(p_expression)) {
		return true;
	}
	resolved_contextual_enum_cases.insert(p_expression);

	BSParser::DataType unqualified_type;
	unqualified_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	unqualified_type.kind = BSParser::DataType::VARIANT;

	if (reference->attribute == nullptr) {
		p_expression->set_datatype(unqualified_type);
		return true;
	}
	const StringName case_name = reference->attribute->name;

	BSParser::DataType enum_meta_type;
	if (!tagged_union_metatype_from_expected_type(p_expected_type, p_expression, enum_meta_type)) {
		const String case_suffix = call != nullptr ? "(...)" : "";
		const bool expected_type_is_nameable = p_expected_type.is_set() && !p_expected_type.has_no_type() &&
				p_expected_type.is_hard_type() && !p_expected_type.is_unresolved_inference_fallback;
		if (expected_type_is_nameable) {
			const String qualified_example = vformat(R"(Result[int, String].%s%s)", case_name, case_suffix);
			push_error(vformat(R"*(Contextual shorthand ".%s" needs an expected tagged-union type, but this position expects "%s"; spell the case with its union, e.g. "%s".)*",
							   case_name, p_expected_type.to_string_diagnostic(), qualified_example),
					p_expression);
			p_expression->set_datatype(unqualified_type);
			return true;
		}
		const String annotated_example = vformat(R"(var x: Result[int, String] = .%s%s)", case_name, case_suffix);
		push_error(vformat(R"*(Contextual shorthand ".%s" needs an expected tagged-union type; annotate the target, e.g. "%s".)*",
						   case_name, annotated_example),
				p_expression);
		p_expression->set_datatype(unqualified_type);
		return true;
	}

	if (!enum_meta_type.enum_values.has(case_name)) {
		push_error(vformat(R"(Tagged union "%s" has no case "%s".)", enum_meta_type.enum_type, case_name), p_expression);
		p_expression->set_datatype(unqualified_type);
		return true;
	}

	const BSParser::DataType case_value_type = type_from_metatype(enum_meta_type);
	const bool carries_payload = enum_meta_type.get_enum_case_payload(case_name) != nullptr;

	if (call == nullptr) {
		if (carries_payload) {
			push_error(vformat(R"*(Enum case "%s.%s" carries a payload and must be constructed, e.g. ".%s(...)".)*",
							   enum_meta_type.enum_type, case_name, case_name),
					p_expression);
			p_expression->set_datatype(case_value_type);
			return true;
		}
		reference->attribute->set_datatype(case_value_type);
		reference->set_datatype(case_value_type);
		reference->is_constant = true;
		reference->reduced_value = _tagged_union_case_singleton(enum_meta_type.enum_values[case_name]);
		return true;
	}

	if (!carries_payload) {
		push_error(vformat(R"*(Enum case "%s.%s" carries no payload, so it is written as a value, e.g. ".%s".)*",
						   enum_meta_type.enum_type, case_name, case_name),
				p_expression);
		p_expression->set_datatype(case_value_type);
		return true;
	}

	reference->attribute->set_datatype(case_value_type);
	reference->set_datatype(enum_meta_type);
	reduce_call_enum_case_construction(call, enum_meta_type);
	return true;
}

void BSAnalyzer::reduce_expression(BSParser::ExpressionNode *p_expression, bool p_is_root) {
	if (p_expression == nullptr || p_expression->reduced) {
		return;
	}
	// Publish visitation before descending. Int-backed enum value cycles revisit the same
	// expression nodes while their declarations carry RESOLVING; marking afterward recurses.
	p_expression->reduced = true;
	switch (p_expression->type) {
		case BSParser::Node::LITERAL:
			reduce_literal(static_cast<BSParser::LiteralNode *>(p_expression));
			break;
		case BSParser::Node::UNARY_OPERATOR:
			reduce_unary_op(static_cast<BSParser::UnaryOpNode *>(p_expression));
			break;
		case BSParser::Node::BINARY_OPERATOR:
			reduce_binary_op(static_cast<BSParser::BinaryOpNode *>(p_expression));
			break;
		case BSParser::Node::IDENTIFIER:
			reduce_identifier(static_cast<BSParser::IdentifierNode *>(p_expression));
			break;
		case BSParser::Node::AWAIT:
			reduce_await(static_cast<BSParser::AwaitNode *>(p_expression));
			break;
		case BSParser::Node::CALL:
			reduce_call(static_cast<BSParser::CallNode *>(p_expression), false, p_is_root);
			break;
		case BSParser::Node::LAMBDA:
			reduce_lambda(static_cast<BSParser::LambdaNode *>(p_expression));
			break;
		case BSParser::Node::SUBSCRIPT:
			reduce_subscript(static_cast<BSParser::SubscriptNode *>(p_expression));
			break;
		case BSParser::Node::ARRAY:
			reduce_array(static_cast<BSParser::ArrayNode *>(p_expression));
			break;
		case BSParser::Node::TUPLE_LITERAL:
			reduce_tuple_literal(static_cast<BSParser::TupleLiteralNode *>(p_expression));
			break;
		case BSParser::Node::DICTIONARY:
			reduce_dictionary(static_cast<BSParser::DictionaryNode *>(p_expression));
			break;
		case BSParser::Node::TERNARY_OPERATOR:
			reduce_ternary(static_cast<BSParser::TernaryOpNode *>(p_expression));
			break;
		case BSParser::Node::CAST:
			reduce_cast(static_cast<BSParser::CastNode *>(p_expression));
			break;
		case BSParser::Node::TYPE_TEST:
			reduce_type_test(static_cast<BSParser::TypeTestNode *>(p_expression));
			break;
		case BSParser::Node::SELF: {
			// Foundry reduce_self @ c9d5e35: expression `self` is the frame's `@Self` type parameter
			// so RETURN-kind Self-contract admission (assign / return) matches Self-typed values.
			BSParser::SelfNode *self_node = static_cast<BSParser::SelfNode *>(p_expression);
			if (current_class != nullptr) {
				self_node->set_datatype(_self_type_parameter_for_class(current_class));
			}
			self_node->reduced = true;
		} break;
		case BSParser::Node::ASSIGNMENT: {
			BSParser::AssignmentNode *assignment = static_cast<BSParser::AssignmentNode *>(p_expression);
			reduce_expression(assignment->assigned_value);
			if (assignment->assignee != nullptr && assignment->assignee->type == BSParser::Node::IDENTIFIER) {
				BSParser::IdentifierNode *assignee = static_cast<BSParser::IdentifierNode *>(assignment->assignee);
				// Foundry fs_analyzer.cpp:7603: only a LOCAL_VARIABLE owns this union arm.
				// Parameters and binds point to smaller nodes without an assignments field.
				if (assignee->source == BSParser::IdentifierNode::LOCAL_VARIABLE && assignee->variable_source != nullptr) {
					assignee->variable_source->assignments++;
				}
			}
			// Foundry reduce_assignment @ c9d5e35: type assignee with narrowing still active so a
			// compound read sees the narrowed width; then restore the declared type onto the node
			// (write address / destination checks) and stash the narrowed type for the compound op.
			reduce_expression(assignment->assignee);

			BSParser::DataType compound_assignment_narrowed_read_type;
			bool has_compound_assignment_narrowed_read_type = false;
			if (assignment->assignee != nullptr && assignment->assignee->type == BSParser::Node::IDENTIFIER) {
				BSParser::IdentifierNode *assignee_identifier = static_cast<BSParser::IdentifierNode *>(assignment->assignee);
				const BSParser::Node *flow_key = flow_finality.flow_narrowing_key_from_identifier(assignee_identifier);
				if (const BSParser::DataType *narrowed_type = flow_finality.lookup_flow_narrowed_type(flow_key)) {
					BSParser::DataType declared_type;
					bool found_declared_type = true;
					switch (assignee_identifier->source) {
						case BSParser::IdentifierNode::FUNCTION_PARAMETER:
							if (assignee_identifier->parameter_source != nullptr) {
								declared_type = assignee_identifier->parameter_source->get_datatype();
							} else {
								found_declared_type = false;
							}
							break;
						case BSParser::IdentifierNode::LOCAL_VARIABLE:
							if (assignee_identifier->variable_source != nullptr) {
								declared_type = assignee_identifier->variable_source->get_datatype();
							} else {
								found_declared_type = false;
							}
							break;
						case BSParser::IdentifierNode::LOCAL_ITERATOR:
							if (assignee_identifier->bind_source != nullptr) {
								declared_type = assignee_identifier->bind_source->get_datatype();
							} else {
								found_declared_type = false;
							}
							break;
						case BSParser::IdentifierNode::LOCAL_BIND:
							if (assignee_identifier->bind_source != nullptr) {
								declared_type = assignee_identifier->bind_source->get_datatype();
								declared_type.is_constant = true;
							} else {
								found_declared_type = false;
							}
							break;
						default:
							found_declared_type = false;
							break;
					}
					if (found_declared_type) {
						if (assignment->operation != BSParser::AssignmentNode::OP_NONE) {
							compound_assignment_narrowed_read_type = *narrowed_type;
							has_compound_assignment_narrowed_read_type = true;
						}
						assignee_identifier->set_datatype(declared_type);
					}
				}
			}

			flow_finality.clear_flow_narrowing(assignment->assignee);
			// Foundry reduce_assignment @ c9d5e35: reject immutable destinations before
			// ordinary value compatibility so a tuple/constant write emits one focused error.
			if (assignment->assignee != nullptr) {
				if (assignment->assignee->type == BSParser::Node::IDENTIFIER) {
					const BSParser::IdentifierNode *identifier = static_cast<const BSParser::IdentifierNode *>(assignment->assignee);
					if (identifier->source == BSParser::IdentifierNode::LOCAL_CONSTANT ||
							identifier->source == BSParser::IdentifierNode::MEMBER_CONSTANT ||
							identifier->source == BSParser::IdentifierNode::LOCAL_BIND) {
						push_error("Cannot assign a new value to a constant.", assignment->assignee);
						assignment->set_datatype(assignment->assigned_value != nullptr ? assignment->assigned_value->get_datatype() : BSParser::DataType());
						break;
					}
				} else if (assignment->assignee->type == BSParser::Node::SUBSCRIPT) {
					const BSParser::SubscriptNode *subscript = static_cast<const BSParser::SubscriptNode *>(assignment->assignee);
					const BSParser::DataType base_type = subscript->base != nullptr ? subscript->base->get_datatype() : BSParser::DataType();
					if (base_type.kind == BSParser::DataType::TUPLE) {
						push_error(vformat(R"*(Cannot assign to an element of tuple "%s"; tuples are immutable.)*", base_type.to_string()), assignment->assignee);
						assignment->set_datatype(assignment->assigned_value != nullptr ? assignment->assigned_value->get_datatype() : BSParser::DataType());
						break;
					}
					const bool resolved_constant_destination = subscript->get_datatype().is_constant ||
							(subscript->attribute != nullptr && subscript->attribute->source == BSParser::IdentifierNode::MEMBER_CONSTANT);
					if (resolved_constant_destination || (subscript->base != nullptr && subscript->base->is_constant) || base_type.is_constant) {
						push_error("Cannot assign a new value to a constant.", assignment->assignee);
						assignment->set_datatype(assignment->assigned_value != nullptr ? assignment->assigned_value->get_datatype() : BSParser::DataType());
						break;
					}
				}
			}
			if (assignment->assignee != nullptr && assignment->assigned_value != nullptr) {
				// Contextual `.Case` on the RHS takes its union from the assignee (@ c9d5e35).
				qualify_contextual_enum_case_consumer(assignment->assigned_value, assignment->assignee->get_datatype());
				mark_coroutine_handle_capture(assignment->assigned_value, assignment->assignee->get_datatype());
			}
			if (assignment->assigned_value != nullptr) {
				BSParser::DataType assignee_type;
				if (assignment->assignee != nullptr) {
					assignee_type = assignment->assignee->get_datatype();
				}
				const bool constant_type_ok = update_constant_expression_type(assignment->assigned_value, assignee_type, "assign");
				BSParser::DataType assigned_value_type = assignment->assigned_value->get_datatype();
				bool compatible = true;
				BSParser::DataType op_type = assigned_value_type;
				// Foundry reduce_assignment compound path @ c9d5e35: left operand is the stashed
				// narrowed read when present, else the (restored) assignee type.
				if (assignment->operation != BSParser::AssignmentNode::OP_NONE && !op_type.is_variant() &&
						assignment->variant_op != Variant::OP_MAX) {
					const BSParser::DataType &compound_operand_type = has_compound_assignment_narrowed_read_type
							? compound_assignment_narrowed_read_type
							: assignee_type;
					op_type = get_operation_type(assignment->variant_op, compound_operand_type, assigned_value_type, compatible, assignment->assigned_value);

					if (assignee_type.is_variant()) {
						mark_node_unsafe(assignment);
					} else if (!compatible) {
						mark_node_unsafe(assignment);
						if (assigned_value_type.is_variant()) {
							assignment->use_conversion_assign = true;
						} else {
							BSParser::DataType assignee_alternative;
							BSParser::DataType assigned_alternative;
							if (_find_unsupported_operand_pair(assignment->variant_op, compound_operand_type, assigned_value_type, assignee_alternative, assigned_alternative)) {
								push_error(_make_set_operation_error(compound_operand_type, assigned_value_type,
												   assignee_alternative, assigned_alternative, assignment->variant_op, String()),
										assignment);
							} else {
								push_error(vformat(R"(Invalid operands "%s" and "%s" for assignment operator.)", assignee_type.to_string(), assigned_value_type.to_string()), assignment);
							}
						}
					}
				}
				assignment->set_datatype(op_type);

				// Foundry reduce_assignment Self-contract RETURN gate @ c9d5e35.
				if (!constant_type_ok) {
					// Value-aware constant reporting already emitted the sole mismatch.
				} else if (!assignee_type.is_variant() && assignee_type.is_set() &&
						_datatype_contains_self_type_parameter(assignee_type)) {
					const bool value_is_gradual = op_type.is_variant() || !op_type.is_hard_type();
					if ((value_is_gradual
										? !self_contract_admits_gradual_value(assignee_type, op_type)
										: !_self_contract_admits_value_type(assignee_type, op_type, BSAnalyzer::SelfContractKind::RETURN, assignment->assigned_value, nullptr))) {
						mark_node_unsafe(assignment);
						push_error(vformat(R"(Value of type "%s" cannot be assigned to a variable of type "%s".)",
										   assigned_value_type.to_string(),
										   assignee_type.to_string()) +
										BSParser::DataType::same_rendered_name_clause(assigned_value_type, "value", assignee_type, "variable's type"),
								assignment->assigned_value);
					} else if (value_is_gradual || op_type.is_variant()) {
						mark_node_unsafe(assignment);
						assignment->use_conversion_assign = true;
					}
				} else if (assignee_type.is_set() && !assignee_type.is_variant() && op_type.is_set()) {
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (assignment->assigned_value->is_constant) {
						options.constant_source_value = &assignment->assigned_value->reduced_value;
					}
					if (!BSTypeCompatibility::check(assignee_type, op_type, options).compatible) {
						push_error(vformat(R"*(Value of type "%s" cannot be assigned to a variable of type "%s".)*",
										   assigned_value_type.to_string(), assignee_type.to_string()) +
										BSParser::DataType::same_rendered_name_clause(assigned_value_type, "value", assignee_type, "variable's type"),
								assignment->assigned_value);
					} else if (op_type.is_variant() || !op_type.is_hard_type()) {
						mark_node_unsafe(assignment);
						assignment->use_conversion_assign = true;
					}
				}
			}
		} break;
		default:
			break;
	}
}

void BSAnalyzer::analyze_statement(BSParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}
	if (p_node->is_expression()) {
		reduce_expression(static_cast<BSParser::ExpressionNode *>(p_node), true);
		return;
	}
	switch (p_node->type) {
		case BSParser::Node::VARIABLE: {
			BSParser::VariableNode *variable = static_cast<BSParser::VariableNode *>(p_node);
			if (variable->initializer != nullptr) {
				reduce_expression(variable->initializer);
			}
			BSParser::DataType declared = variable->get_datatype();
			if (variable->datatype_specifier != nullptr) {
				declared = datatype_from_type_node(variable->datatype_specifier);
				variable->set_datatype(declared);
			}
			if (variable->initializer != nullptr) {
				// Foundry assignable path: contextual `.Case` takes its union from the declared type.
				qualify_contextual_enum_case_consumer(variable->initializer, declared);
				mark_coroutine_handle_capture(variable->initializer, declared);
				// Foundry resolve_assignable @ c9d5e35: `:=` preserves the initializer's complete
				// analyzer type, including Callable/Signal signatures used by later calls.
				if ((!declared.is_set() || declared.is_variant()) && variable->infer_datatype &&
						variable->initializer->get_datatype().is_set()) {
					variable->set_datatype(variable->initializer->get_datatype());
					declared = variable->get_datatype();
				}
			}
			if (declared.is_set() && !declared.is_variant() && variable->initializer != nullptr && variable->initializer->get_datatype().is_set()) {
				const bool constant_type_ok = update_constant_expression_type(variable->initializer, declared, "assign");
				const BSParser::DataType initializer_type = variable->initializer->get_datatype();
				// Foundry assignable Self-contract RETURN gate @ c9d5e35.
				if (!constant_type_ok) {
					// Value-aware constant reporting already emitted the sole mismatch.
				} else if (_datatype_contains_self_type_parameter(declared)) {
					const bool value_is_gradual = initializer_type.is_variant() || !initializer_type.is_hard_type();
					if ((value_is_gradual
										? !self_contract_admits_gradual_value(declared, initializer_type)
										: !_self_contract_admits_value_type(declared, initializer_type, BSAnalyzer::SelfContractKind::RETURN, variable->initializer, nullptr))) {
						push_error(vformat(R"(Cannot assign a value of type "%s" to a variable of type "%s".)",
										   initializer_type.to_string(), declared.to_string()) +
										BSParser::DataType::same_rendered_name_clause(initializer_type, "value", declared, "specified type"),
								variable->initializer);
					} else if (value_is_gradual || initializer_type.is_variant()) {
						mark_node_unsafe(variable->initializer);
						variable->use_conversion_assign = true;
					}
				} else {
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (variable->initializer->is_constant) {
						options.constant_source_value = &variable->initializer->reduced_value;
					}
					bool compatible = BSTypeCompatibility::check(declared, initializer_type, options).compatible;
					if ((declared.kind == BSParser::DataType::TUPLE || initializer_type.kind == BSParser::DataType::TUPLE) &&
							!_datatype_strict_identity_equal(declared, initializer_type)) {
						compatible = false;
					}
					if (!compatible) {
						if (declared.kind == BSParser::DataType::TUPLE || initializer_type.kind == BSParser::DataType::TUPLE) {
							push_error(vformat(R"(Cannot assign a value of type %s to variable "%s" with specified type %s.)",
											   initializer_type.to_string(),
											   variable->identifier != nullptr ? String(variable->identifier->name) : String("<unknown>"),
											   declared.to_string()) +
											BSParser::DataType::same_rendered_name_clause(initializer_type, "value", declared, "specified type"),
									variable->initializer);
						} else {
							push_error(vformat(R"(Cannot assign a value of type "%s" to a variable of type "%s".)",
											   initializer_type.to_string(), declared.to_string()) +
											BSParser::DataType::same_rendered_name_clause(initializer_type, "value", declared, "specified type"),
									variable->initializer);
						}
					}
				}
			}
		} break;
		case BSParser::Node::CONSTANT: {
			BSParser::ConstantNode *constant = static_cast<BSParser::ConstantNode *>(p_node);
			if (constant->initializer != nullptr) {
				reduce_expression(constant->initializer);
			}
			BSParser::DataType declared = constant->get_datatype();
			if (constant->datatype_specifier != nullptr) {
				declared = datatype_from_type_node(constant->datatype_specifier);
				constant->set_datatype(declared);
			}
			if (constant->initializer != nullptr) {
				qualify_contextual_enum_case_consumer(constant->initializer, declared);
				mark_coroutine_handle_capture(constant->initializer, declared);
				if ((!declared.is_set() || declared.is_variant()) && constant->initializer->is_constant &&
						!constant->initializer->get_datatype().is_set()) {
					constant->initializer->set_datatype(type_from_variant(constant->initializer->reduced_value));
					constant->set_datatype(constant->initializer->get_datatype());
				} else if ((!declared.is_set() || declared.is_variant()) && constant->initializer->get_datatype().is_set()) {
					constant->set_datatype(constant->initializer->get_datatype());
				}
			}
			if (declared.is_set() && !declared.is_variant() && constant->initializer != nullptr && constant->initializer->get_datatype().is_set()) {
				const bool constant_type_ok = update_constant_expression_type(constant->initializer, declared, "assign");
				const BSParser::DataType initializer_type = constant->initializer->get_datatype();
				if (!constant_type_ok) {
					// Value-aware constant reporting already emitted the sole mismatch.
				} else if (_datatype_contains_self_type_parameter(declared)) {
					const bool value_is_gradual = initializer_type.is_variant() || !initializer_type.is_hard_type();
					if ((value_is_gradual
										? !self_contract_admits_gradual_value(declared, initializer_type)
										: !_self_contract_admits_value_type(declared, initializer_type, BSAnalyzer::SelfContractKind::RETURN, constant->initializer, nullptr))) {
						push_error(vformat(R"(Cannot assign a value of type "%s" to a constant of type "%s".)",
										   initializer_type.to_string(), declared.to_string()) +
										BSParser::DataType::same_rendered_name_clause(initializer_type, "value", declared, "specified type"),
								constant->initializer);
					}
				} else {
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (constant->initializer->is_constant) {
						options.constant_source_value = &constant->initializer->reduced_value;
					}
					if (!BSTypeCompatibility::check(declared, initializer_type, options).compatible) {
						push_error(vformat(R"(Cannot assign a value of type "%s" to a constant of type "%s".)",
										   initializer_type.to_string(), declared.to_string()) +
										BSParser::DataType::same_rendered_name_clause(initializer_type, "value", declared, "specified type"),
								constant->initializer);
					}
				}
			}
		} break;
		case BSParser::Node::RETURN: {
			BSParser::ReturnNode *ret = static_cast<BSParser::ReturnNode *>(p_node);
			if (ret->return_value != nullptr) {
				reduce_expression(ret->return_value);
				// Foundry: contextual `.Case` return takes its union from the function return type.
				BSParser::DataType expected_return;
				if (current_function != nullptr) {
					expected_return = current_function->get_datatype();
				}
				qualify_contextual_enum_case_consumer(ret->return_value, expected_return);
				if (expected_return.is_set()) {
					mark_coroutine_handle_capture(ret->return_value, expected_return);
				}
				const bool constant_type_ok = update_constant_expression_type(ret->return_value, expected_return, "return");
				const bool returns_void = expected_return.is_set() && expected_return.kind == BSParser::DataType::BUILTIN &&
						expected_return.builtin_type == Variant::NIL;
				if (!constant_type_ok) {
					break;
				}
				if (returns_void) {
					push_error("A void function cannot return a value.", ret);
					break;
				}
				// Foundry resolve_return Self-contract RETURN gate @ c9d5e35.
				// Static frames substitute Self to the declaring class (concrete), matching
				// get_function_signature's static substitution; instance frames keep the contract.
				const bool preserve_self_contract = current_class != nullptr && current_function != nullptr &&
						!current_function->is_abstract && !current_function->is_static &&
						expected_return.is_set() && !expected_return.is_variant() &&
						_datatype_contains_self_type_parameter(expected_return);
				if (preserve_self_contract) {
					const BSParser::DataType result = ret->return_value->get_datatype();
					const bool value_is_gradual = result.is_variant() || !result.is_hard_type();
					if ((value_is_gradual
										? !self_contract_admits_gradual_value(expected_return, result)
										: !_self_contract_admits_value_type(expected_return, result, BSAnalyzer::SelfContractKind::RETURN, ret->return_value, nullptr))) {
						push_error(vformat(R"(Cannot return value of type "%s" because the function return type is "%s".)",
										   result.to_string(),
										   expected_return.to_string()) +
										BSParser::DataType::same_rendered_name_clause(result, "returned value", expected_return, "return type"),
								ret);
					} else if (value_is_gradual || result.is_variant()) {
						mark_node_unsafe(ret);
					}
				} else if (expected_return.is_set() && !expected_return.is_variant() &&
						_datatype_contains_self_type_parameter(expected_return) &&
						current_function != nullptr && current_function->is_static) {
					const BSParser::DataType compatibility_expected =
							_substitute_self_type_parameter(expected_return, _self_type_for_class(current_class));
					const BSParser::DataType result = ret->return_value->get_datatype();
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (!BSTypeCompatibility::check(compatibility_expected, result, options).compatible) {
						push_error(vformat(R"(Cannot return value of type "%s" because the function return type is "%s".)",
										   result.to_string(),
										   expected_return.to_string()) +
										BSParser::DataType::same_rendered_name_clause(result, "returned value", expected_return, "return type"),
								ret);
					}
				} else if (expected_return.is_set() && !expected_return.is_variant()) {
					const BSParser::DataType result = ret->return_value->get_datatype();
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (ret->return_value->is_constant) {
						options.constant_source_value = &ret->return_value->reduced_value;
					}
					if (!BSTypeCompatibility::check(expected_return, result, options).compatible) {
						push_error(vformat(R"*(Cannot return value of type "%s" because the function return type is "%s".)*",
										   result.to_string(), expected_return.to_string()) +
										BSParser::DataType::same_rendered_name_clause(result, "returned value", expected_return, "return type"),
								ret);
					} else if (result.is_variant() || !result.is_hard_type()) {
						mark_node_unsafe(ret);
					}
				}
			} else if (current_function != nullptr) {
				const BSParser::DataType expected_return = current_function->get_datatype();
				if (expected_return.is_set() && !expected_return.is_variant() &&
						!(expected_return.kind == BSParser::DataType::BUILTIN && expected_return.builtin_type == Variant::NIL)) {
					push_error(vformat(R"*(Cannot return without a value because the function return type is "%s".)*",
									   expected_return.to_string()),
							ret);
				}
			}
		} break;
		case BSParser::Node::IF: {
			analyze_if(static_cast<BSParser::IfNode *>(p_node));
		} break;
		case BSParser::Node::WHILE: {
			BSParser::WhileNode *while_node = static_cast<BSParser::WhileNode *>(p_node);
			flow_finality.reduce_condition_expression(while_node->condition);
			HashMap<const BSParser::Node *, BSParser::DataType> previous_flow_narrowed_types(flow_finality.get_flow_narrowed_types());
			flow_finality.apply_flow_narrowing_from_condition(while_node->condition, true);
			analyze_suite(while_node->loop);
			flow_finality.get_flow_narrowed_types() = previous_flow_narrowed_types;
		} break;
		case BSParser::Node::FOR: {
			BSParser::ForNode *for_node = static_cast<BSParser::ForNode *>(p_node);
			reduce_expression(for_node->list);
			// Foundry resolve_for: range is variadic in ordinary call metadata, but the for
			// intrinsic requires 1..3 operands and yields an int iterator. Do not allocate it.
			if (for_node->list != nullptr && for_node->list->type == BSParser::Node::CALL) {
				BSParser::CallNode *call = static_cast<BSParser::CallNode *>(for_node->list);
				if (call->callee != nullptr && call->callee->type == BSParser::Node::IDENTIFIER) {
					const BSParser::IdentifierNode *identifier = static_cast<BSParser::IdentifierNode *>(call->callee);
					if (identifier->name == SNAME("range") && identifier->source == BSParser::IdentifierNode::UNDEFINED_SOURCE &&
							identifier->get_datatype().has_method_signature) {
						List<BSParser::DataType> range_parameters;
						for (int i = 0; i < 3; i++) {
							range_parameters.push_back(type_from_property(PropertyInfo(Variant::NIL, ""), true));
						}
						call_site_validation.validate_call_arg(range_parameters, 2, false, call);
						if (for_node->variable != nullptr) {
							for_node->variable->set_datatype(type_from_property(PropertyInfo(Variant::INT, "")));
						}
					}
				}
			}
			analyze_suite(for_node->loop);
		} break;
		case BSParser::Node::MATCH: {
			resolve_match(static_cast<BSParser::MatchNode *>(p_node));
		} break;
		case BSParser::Node::ASSERT: {
			BSParser::AssertNode *assert_node = static_cast<BSParser::AssertNode *>(p_node);
			flow_finality.reduce_condition_expression(assert_node->condition);
			reduce_expression(assert_node->message);
			// Foundry resolve_assert: successful assert keeps true-branch narrowing for later statements.
			flow_finality.apply_flow_narrowing_from_condition(assert_node->condition, true);
		} break;
		case BSParser::Node::SUITE:
			analyze_suite(static_cast<BSParser::SuiteNode *>(p_node));
			break;
		default:
			break;
	}
}

void BSAnalyzer::analyze_suite(BSParser::SuiteNode *p_suite) {
	if (p_suite == nullptr) {
		return;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		analyze_statement(p_suite->statements[i]);
		// Foundry resolve_suite @ c9d5e35: flush lambda bodies after each statement so capture
		// marking sees the outer suite's live flow-narrowing map.
		resolve_pending_lambda_bodies();
	}
}

void BSAnalyzer::analyze_function_body(BSParser::FunctionNode *p_function, bool p_is_lambda) {
	if (p_function == nullptr || p_function->resolved_body) {
		return;
	}
	p_function->resolved_body = true;
	BSParser::FunctionNode *previous = current_function;
	current_function = p_function;
	// Foundry applies function annotations before body analysis (resolve_class_body @ c9d5e35).
	for (BSParser::AnnotationNode *annotation : p_function->annotations) {
		if (annotation != nullptr) {
			resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_METHOD);
			annotation->apply(parser, p_function, current_class);
		}
	}
	if (!p_function->has_body) {
		if (!p_function->is_abstract) {
			push_error(vformat(R"(Function "%s" must have a body or be declared abstract.)", p_function->identifier != nullptr ? p_function->identifier->name : StringName()), p_function);
		}
		current_function = previous;
		return;
	}
	{
		// Foundry resolve_function_body @ c9d5e35: lambda bodies must not clear the outer
		// function's captured-source map (marks from reduce_identifier must survive).
		FlowFinalityContext::FlowNarrowingScope flow_scope(flow_finality, !p_is_lambda);
		analyze_suite(p_function->body);
	}
	warn_unused_parameters(p_function);
	warn_unused_locals(p_function->body);
	current_function = previous;
}

void BSAnalyzer::warn_unused_parameters(BSParser::FunctionNode *p_function) {
#ifdef DEBUG_ENABLED
	if (p_function == nullptr || p_function->is_abstract) {
		return;
	}
	const String function_visible_name = p_function->identifier != nullptr ? String(p_function->identifier->name) : String("<anonymous>");
	for (int i = 0; i < p_function->parameters.size(); i++) {
		BSParser::ParameterNode *parameter = p_function->parameters[i];
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		if (parameter->usages == 0 && !String(parameter->identifier->name).begins_with("_")) {
			Vector<String> symbols;
			symbols.push_back(function_visible_name);
			symbols.push_back(String(parameter->identifier->name));
			push_warning(parameter, BSWarning::UNUSED_PARAMETER, symbols);
		}
	}
	if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr) {
		if (p_function->rest_parameter->usages == 0 && !String(p_function->rest_parameter->identifier->name).begins_with("_")) {
			Vector<String> symbols;
			symbols.push_back(function_visible_name);
			symbols.push_back(String(p_function->rest_parameter->identifier->name));
			push_warning(p_function->rest_parameter, BSWarning::UNUSED_PARAMETER, symbols);
		}
	}
#else
	(void)p_function;
#endif
}

void BSAnalyzer::warn_unused_locals(BSParser::SuiteNode *p_suite) {
#ifdef DEBUG_ENABLED
	if (p_suite == nullptr) {
		return;
	}
	for (int i = 0; i < p_suite->locals.size(); i++) {
		const BSParser::SuiteNode::Local &local = p_suite->locals[i];
		if (local.type == BSParser::SuiteNode::Local::VARIABLE && local.variable != nullptr && local.variable->identifier != nullptr) {
			if (local.variable->usages == 0 && !String(local.variable->identifier->name).begins_with("_")) {
				Vector<String> symbols;
				symbols.push_back(String(local.variable->identifier->name));
				push_warning(local.variable, BSWarning::UNUSED_VARIABLE, symbols);
			}
		} else if (local.type == BSParser::SuiteNode::Local::CONSTANT && local.constant != nullptr && local.constant->identifier != nullptr) {
			if (local.constant->usages == 0 && !String(local.constant->identifier->name).begins_with("_")) {
				Vector<String> symbols;
				symbols.push_back(String(local.constant->identifier->name));
				push_warning(local.constant, BSWarning::UNUSED_LOCAL_CONSTANT, symbols);
			}
		}
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		BSParser::Node *statement = p_suite->statements[i];
		if (statement == nullptr) {
			continue;
		}
		if (statement->type == BSParser::Node::SUITE) {
			warn_unused_locals(static_cast<BSParser::SuiteNode *>(statement));
		} else if (statement->type == BSParser::Node::IF) {
			BSParser::IfNode *if_node = static_cast<BSParser::IfNode *>(statement);
			warn_unused_locals(if_node->true_block);
			warn_unused_locals(if_node->false_block);
		} else if (statement->type == BSParser::Node::WHILE) {
			warn_unused_locals(static_cast<BSParser::WhileNode *>(statement)->loop);
		} else if (statement->type == BSParser::Node::FOR) {
			warn_unused_locals(static_cast<BSParser::ForNode *>(statement)->loop);
		} else if (statement->type == BSParser::Node::MATCH) {
			BSParser::MatchNode *match_node = static_cast<BSParser::MatchNode *>(statement);
			for (int b = 0; b < match_node->branches.size(); b++) {
				if (match_node->branches[b] != nullptr) {
					warn_unused_locals(match_node->branches[b]->block);
				}
			}
		}
	}
#else
	(void)p_suite;
#endif
}

void BSAnalyzer::analyze_class_body(BSParser::ClassNode *p_class, const BSParser::Node *p_source) {
	// Foundry resolve_class_body @ c9d5e35 (`fs_analyzer.cpp` ~3545): owner BODY failure
	// memoization, foreign SCRIPT raise under ForeignAnalyzerVisibilityScope, and dependent
	// "Could not resolve class" replay with path/first-error suffix when available.
	if (p_class == nullptr) {
		return;
	}
	ERR_FAIL_NULL(parser);

	const bool owns_class = parser->has_class(p_class) || p_class->is_native_conformance_shim || p_class->is_builtin_conformance_shim;
	if (p_source == nullptr && owns_class) {
		p_source = p_class;
	}

	Ref<BSParserRef> parser_ref;
	if (!owns_class) {
		const String path = _class_script_path_for_foreign_resolve(p_class);
		if (!path.is_empty()) {
			Error err = OK;
			parser_ref = BSCache::get_parser(path, BSParserRef::PARSED, err, parser->script_path);
			if (err != OK) {
				parser_ref = Ref<BSParserRef>();
			}
		}
	}

	const int body_error_count = parser->get_errors().size();
	Finally record_body_failure([&]() {
		if (owns_class && parser->get_errors().size() > body_error_count &&
				!errors_from_index_are_only_m5_deferred(body_error_count)) {
			owner_resolution_failures.record_class(p_class, OwnerResolutionFailures::BODY, body_error_count);
		}
	});

	auto push_external_body_failure = [&](int p_first_error_index) {
		if (!dependent_resolution_failure_replays.record_class(
					p_class, OwnerResolutionFailures::BODY)) {
			return;
		}
		String message = vformat(R"(Could not resolve class "%s".)", bs_class_or_trait_diagnostic_name(p_class));
		String class_path;
		if (parser_ref.is_valid()) {
			class_path = parser_ref->get_path();
		}
		if (class_path.is_empty()) {
			class_path = p_class->get_datatype().script_path;
		}
		// The path adds nothing when it is the same file the class name already came from.
		if (bs_diagnostic_file_reference(class_path) == bs_diagnostic_file_reference(p_class->fqcn)) {
			class_path = String();
		}
		BSParser *dependency_parser = parser_ref.is_valid() ? parser_ref->get_parser() : nullptr;
		const String suffix = _dependency_error_suffix(
				"class", class_path, dependency_parser, p_first_error_index);
		if (!suffix.is_empty()) {
			message += " " + suffix;
		}
		push_error(message, p_source);
	};

	if (p_class->resolved_body) {
		if (!owns_class && parser_ref.is_valid() && parser_ref->get_analyzer() != nullptr) {
			BSAnalyzer *other_analyzer = parser_ref->get_analyzer();
			if (other_analyzer->owner_resolution_failures.has_class(p_class, OwnerResolutionFailures::BODY)) {
				push_external_body_failure(
						other_analyzer->owner_resolution_failures.first_error_index(
								p_class, OwnerResolutionFailures::BODY));
			}
		}
		return;
	}

	if (!owns_class) {
		if (parser_ref.is_null() || parser_ref->get_parser() == nullptr) {
			push_error(vformat(R"(Could not resolve class "%s".)", bs_class_or_trait_diagnostic_name(p_class)), p_source);
			return;
		}

		Error err = parser_ref->raise_status(BSParserRef::PARSED);
		if (err != OK) {
			const String path = _class_script_path_for_foreign_resolve(p_class);
			push_error(vformat(R"(Could not parse script "%s" (While resolving class body).)",
							   bs_diagnostic_file_reference(path.is_empty() ? p_class->get_datatype().script_path : path)),
					p_source);
			return;
		}

		BSAnalyzer *other_analyzer = parser_ref->get_analyzer();
		BSParser *other_parser = parser_ref->get_parser();
		if (other_analyzer == nullptr || other_parser == nullptr) {
			push_error(vformat(R"(Could not resolve class "%s".)", bs_class_or_trait_diagnostic_name(p_class)), p_source);
			return;
		}

		const int error_count = other_parser->get_errors().size();
		// Raising a dependency class-by-class bypasses the end-of-phase contextual-shorthand sweep;
		// those shorthands are swept when the dependency is analyzed as its own file (Foundry).
		ForeignAnalyzerVisibilityScope visibility_scope(other_analyzer);
		other_analyzer->analyze_class_body(p_class);
		const bool owner_grew_hard_errors = other_parser->get_errors().size() > error_count &&
				!other_analyzer->errors_from_index_are_only_m5_deferred(error_count);
		if (owner_grew_hard_errors ||
				other_analyzer->owner_resolution_failures.has_class(p_class, OwnerResolutionFailures::BODY)) {
			int first_error_index = error_count;
			if (other_analyzer->owner_resolution_failures.has_class(
						p_class, OwnerResolutionFailures::BODY)) {
				first_error_index = other_analyzer->owner_resolution_failures.first_error_index(
						p_class, OwnerResolutionFailures::BODY);
			}
			push_external_body_failure(first_error_index);
		}
		return;
	}

	p_class->resolved_body = true;
	BSParser::ClassNode *previous = current_class;
	current_class = p_class;

	analyze_class_interface(p_class, p_source);
	if (owner_resolution_failures.has_class(p_class, OwnerResolutionFailures::INTERFACE)) {
		owner_resolution_failures.record_class(p_class, OwnerResolutionFailures::BODY,
				owner_resolution_failures.first_error_index(p_class, OwnerResolutionFailures::INTERFACE));
	}

	if (p_class->base_type.kind == BSParser::DataType::CLASS && p_class->base_type.class_type != nullptr) {
		BSParser::ClassNode *base_class = p_class->base_type.class_type;
		analyze_class_body(base_class, p_class);
		if (owner_resolution_failures.has_class(base_class, OwnerResolutionFailures::BODY)) {
			owner_resolution_failures.record_class(p_class, OwnerResolutionFailures::BODY,
					owner_resolution_failures.first_error_index(
							base_class, OwnerResolutionFailures::BODY));
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		switch (member.type) {
			case BSParser::ClassNode::Member::CLASS:
				analyze_class_body(member.m_class, p_source);
				break;
			case BSParser::ClassNode::Member::FUNCTION:
				analyze_function_body(member.function);
				break;
			case BSParser::ClassNode::Member::VARIABLE:
				if (member.variable != nullptr) {
					// Foundry surface applies VARIABLE annotations before body/finality checks
					// (resolve_class_body @ c9d5e35) so `@onready` is visible to final-member rules.
					for (BSParser::AnnotationNode *annotation : member.variable->annotations) {
						if (annotation != nullptr) {
							resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_VARIABLE);
							annotation->apply(parser, member.variable, current_class);
						}
					}
					if (member.variable->initializer != nullptr) {
						reduce_expression(member.variable->initializer);
						qualify_contextual_enum_case_consumer(member.variable->initializer, member.variable->get_datatype());
					}
				}
				break;
			case BSParser::ClassNode::Member::CONSTANT:
				if (member.constant != nullptr) {
					for (BSParser::AnnotationNode *annotation : member.constant->annotations) {
						if (annotation != nullptr) {
							resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_CONSTANT);
							annotation->apply(parser, member.constant, current_class);
						}
					}
					if (member.constant->datatype_specifier != nullptr) {
						member.constant->set_datatype(datatype_from_type_node(member.constant->datatype_specifier));
					}
					if (member.constant->initializer != nullptr) {
						reduce_expression(member.constant->initializer);
						qualify_contextual_enum_case_consumer(member.constant->initializer, member.constant->get_datatype());
						if (member.constant->initializer->is_constant &&
								(!member.constant->get_datatype().is_set() || member.constant->get_datatype().is_variant()) &&
								!member.constant->initializer->get_datatype().is_set()) {
							member.constant->initializer->set_datatype(type_from_variant(member.constant->initializer->reduced_value));
						}
					}
				}
				break;
			case BSParser::ClassNode::Member::SIGNAL:
				if (member.signal != nullptr) {
					for (BSParser::AnnotationNode *annotation : member.signal->annotations) {
						if (annotation != nullptr) {
							resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_SIGNAL);
							annotation->apply(parser, member.signal, current_class);
						}
					}
				}
				break;
			default:
				break;
		}
	}
	warn_unused_class_members(p_class);
	if (!pending_lambda_bodies.is_empty()) {
		// Foundry resolve_class_body @ c9d5e35: any leftover pending lambdas (e.g. class-level
		// initializers) must still resolve before leaving the body phase.
		resolve_pending_lambda_bodies();
	}
	current_class = previous;
}

bool BSAnalyzer::match_branch_always_matches(const BSParser::MatchBranchNode *p_branch) {
	if (p_branch == nullptr || p_branch->guard_body != nullptr) {
		return false; // A guard can fail, so the branch is not guaranteed to run.
	}
	for (int i = 0; i < p_branch->patterns.size(); i++) {
		const BSParser::PatternNode *pattern = p_branch->patterns[i];
		// Only the type-test shape is consulted here. A tuple pattern can also be irrefutable, but
		// whether that closes a match's no-match path is a separate question from this one.
		if (pattern != nullptr && pattern->is_subject_type_test && pattern->is_irrefutable) {
			return true;
		}
	}
	return false;
}

void BSAnalyzer::check_match_exhaustiveness(BSParser::MatchNode *p_match) {
	if (p_match == nullptr) {
		return;
	}
	p_match->covers_subject_domain = false;
	p_match->subject_domain_name = String();
	p_match->uncovered_domain_values = String();
	p_match->uncovered_case_names.clear();
	p_match->uncovered_includes_null = false;
	p_match->subject_domain_is_open_enum = false;

	if (p_match->test == nullptr) {
		return; // Parse error: `match` with no test expression.
	}
	const BSParser::DataType &match_type = p_match->test->get_datatype();
	if (!match_type.is_set()) {
		return; // Type unknown; cannot classify the domain.
	}

	// A branch counts as a default with an unguarded wildcard/bind pattern, or with a same-subject type
	// test that accepts the subject's whole domain. The parser already clears `has_wildcard` when a
	// guard is present.
	bool has_default = false;
	for (int i = 0; i < p_match->branches.size(); i++) {
		BSParser::MatchBranchNode *branch = p_match->branches[i];
		if (branch != nullptr && (branch->has_wildcard || match_branch_always_matches(branch))) {
			has_default = true;
			break;
		}
	}

	// A branch that always matches leaves no fallthrough whatever the subject's domain looks like, so
	// this settles coverage before the domain is classified.
	if (has_default) {
		p_match->covers_subject_domain = true;
		return;
	}

	// Classify the matched type's domain.
	// `domain_values` maps each value's display name to its integer value.
	// Iteration order follows insertion order (Godot HashMap), i.e. enum
	// declaration order, so the unhandled list is deterministic.
	// A plain enum is an open domain rather than a finite one: its declared members name values, but
	// the integer carrier accepts undeclared values, so no set of value patterns closes the match.
	const bool is_tagged_union = match_type.is_tagged_union_type();
	const bool is_plain_enum = match_type.kind == BSParser::DataType::ENUM && !match_type.is_tagged_union;
	bool is_finite_domain = is_tagged_union;
	HashMap<StringName, int64_t> domain_values;
	String type_name;
	if (is_tagged_union) {
		type_name = match_type.enum_type;
	} else if (is_plain_enum) {
		domain_values = match_type.enum_values;
		type_name = match_type.enum_type;
	} else if (match_type.kind == BSParser::DataType::BUILTIN && match_type.builtin_type == Variant::BOOL) {
		is_finite_domain = true;
		domain_values[SNAME("false")] = 0;
		domain_values[SNAME("true")] = 1;
		type_name = "bool";
	}

	if (!is_finite_domain && !is_plain_enum) {
#ifdef DEBUG_ENABLED
		push_warning(p_match, BSWarning::MATCH_WITHOUT_DEFAULT);
#endif
		return;
	}

	if (is_plain_enum) {
		p_match->subject_domain_name = type_name;
		p_match->subject_domain_is_open_enum = true;
	}

	Vector<String> unhandled;
	const bool coverage_is_provable = is_tagged_union
			? collect_uncovered_tagged_union_cases(p_match, match_type, unhandled)
			: collect_uncovered_domain_values(p_match, match_type, domain_values, unhandled);
	if (coverage_is_provable) {
		p_match->subject_domain_name = type_name;
		p_match->covers_subject_domain = is_finite_domain && unhandled.is_empty();
	}

	const bool has_unhandled_values = coverage_is_provable && !unhandled.is_empty();
#ifdef DEBUG_ENABLED
	// A plain enum without a catch-all always leaves undeclared carrier values unhandled, so it owns a
	// diagnostic of its own whether or not declared members are missing too.
	if (is_plain_enum && !has_unhandled_values) {
		Vector<String> symbols;
		symbols.push_back(type_name);
		symbols.push_back(String());
		push_warning(p_match, BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT, symbols);
	}
#endif
	if (!has_unhandled_values) {
		return; // Coverage is settled, or could not be determined and the match stays non-covering.
	}

	// Structured coverage is published for tagged unions only, where each uncovered entry is a case
	// name (or the `null` value of a nullable subject) that tooling can turn back into a pattern.
	if (is_tagged_union) {
		for (int u = 0; u < unhandled.size(); u++) {
			const String &value = unhandled[u];
			if (match_type.is_nullable && value == "null") {
				p_match->uncovered_includes_null = true;
			} else {
				p_match->uncovered_case_names.push_back(StringName(value));
			}
		}
	}

	PackedStringArray uncovered_packed;
	for (int u = 0; u < unhandled.size(); u++) {
		uncovered_packed.push_back(unhandled[u]);
	}
	p_match->uncovered_domain_values = String(", ").join(uncovered_packed);
#ifdef DEBUG_ENABLED
	Vector<String> symbols;
	symbols.push_back(type_name);
	symbols.push_back(p_match->uncovered_domain_values);
	push_warning(p_match, is_plain_enum ? BSWarning::OPEN_ENUM_MATCH_WITHOUT_DEFAULT : BSWarning::NON_EXHAUSTIVE_MATCH, symbols);
#endif
}

bool BSAnalyzer::collect_uncovered_domain_values(const BSParser::MatchNode *p_match, const BSParser::DataType &p_match_type, const HashMap<StringName, int64_t> &p_domain_values, Vector<String> &r_uncovered) const {
	if (p_match == nullptr || p_domain_values.is_empty()) {
		return false; // Nothing to check; claim no coverage.
	}

	// `match` compares typeof() before value, so only same-typed constants can
	// cover a value at runtime: INT for enums, BOOL for the bool domain.
	const Variant::Type expected_type = p_match_type.kind == BSParser::DataType::ENUM ? Variant::INT : Variant::BOOL;

	// For nullable types, `null` (`Variant::NIL`) is a valid runtime value that
	// no enum integer or bool constant can cover, so it must be handled by an
	// explicit `null` pattern (or a wildcard) to be exhaustive.
	const bool domain_includes_null = p_match_type.is_nullable;

	bool null_covered = false;
	HashSet<int64_t> covered_values;
	for (int b = 0; b < p_match->branches.size(); b++) {
		const BSParser::MatchBranchNode *branch = p_match->branches[b];
		if (branch == nullptr || branch->guard_body != nullptr) {
			continue; // Guard may fail; does not guarantee coverage.
		}
		for (int p = 0; p < branch->patterns.size(); p++) {
			const BSParser::PatternNode *pattern = branch->patterns[p];
			if (pattern == nullptr) {
				continue;
			}
			const BSParser::ExpressionNode *value_node = nullptr;
			if (pattern->pattern_type == BSParser::PatternNode::PT_LITERAL) {
				value_node = pattern->literal;
			} else if (pattern->pattern_type == BSParser::PatternNode::PT_EXPRESSION) {
				value_node = pattern->expression;
			} else {
				// Array/dictionary patterns cannot cover enum integers or bool values.
				continue;
			}

			if (value_node == nullptr || !value_node->is_constant) {
				return false; // Non-constant pattern: cannot prove coverage; bail out.
			}
			if (value_node->reduced_value.get_type() != expected_type) {
				// A `null` pattern covers the nullable domain's `null` value.
				if (domain_includes_null && value_node->reduced_value.get_type() == Variant::NIL) {
					null_covered = true;
				}
				// A different-typed constant can never match this domain at
				// runtime (match compares typeof() first), so it covers nothing.
				continue;
			}
			covered_values.insert((int64_t)value_node->reduced_value);
		}
	}

	for (const KeyValue<StringName, int64_t> &E : p_domain_values) {
		if (!covered_values.has(E.value)) {
			r_uncovered.push_back(String(E.key));
		}
	}
	if (domain_includes_null && !null_covered) {
		r_uncovered.push_back("null");
	}
	return true;
}

BSAnalyzer::TaggedUnionPatternCoverage BSAnalyzer::tagged_union_pattern_coverage(const BSParser::PatternNode *p_pattern, const BSParser::DataType &p_match_type, int64_t &r_covered_tag) {
	if (p_pattern == nullptr) {
		return TAGGED_UNION_PATTERN_COVERS_NOTHING;
	}

	if (p_pattern->pattern_type == BSParser::PatternNode::PT_ENUM_CASE) {
		const BSParser::DataType &case_type = p_pattern->case_datatype;
		if (!p_pattern->case_payload_is_irrefutable || case_type.enum_type != p_match_type.enum_type) {
			return TAGGED_UNION_PATTERN_COVERS_NOTHING; // A refutable payload pattern proves nothing about the case.
		}
		const int64_t *tag = case_type.enum_values.getptr(case_type.enum_case_name);
		if (tag == nullptr) {
			return TAGGED_UNION_PATTERN_COVERS_NOTHING;
		}
		r_covered_tag = *tag;
		return TAGGED_UNION_PATTERN_COVERS_CASE;
	}

	const BSParser::ExpressionNode *value_node = nullptr;
	if (p_pattern->pattern_type == BSParser::PatternNode::PT_LITERAL) {
		value_node = p_pattern->literal;
	} else if (p_pattern->pattern_type == BSParser::PatternNode::PT_EXPRESSION) {
		value_node = p_pattern->expression;
	} else {
		return TAGGED_UNION_PATTERN_COVERS_NOTHING; // Array, dictionary and tuple patterns cannot cover a whole case.
	}

	if (value_node == nullptr || !value_node->is_constant) {
		return TAGGED_UNION_PATTERN_COVERAGE_UNPROVABLE;
	}
	if (value_node->reduced_value.get_type() == Variant::NIL) {
		return TAGGED_UNION_PATTERN_COVERS_NULL;
	}
	// A payload-less case folds to its read-only `[tag]` singleton, which is the only constant
	// that can cover a case at runtime.
	const BSParser::DataType &value_type = value_node->get_datatype();
	if (value_node->reduced_value.get_type() != Variant::ARRAY || !value_type.is_tagged_union_type() || value_type.enum_type != p_match_type.enum_type) {
		return TAGGED_UNION_PATTERN_COVERS_NOTHING;
	}
	const Array value = value_node->reduced_value;
	if (value.size() != 1 || value[0].get_type() != Variant::INT) {
		return TAGGED_UNION_PATTERN_COVERS_NOTHING;
	}
	r_covered_tag = (int64_t)value[0];
	return TAGGED_UNION_PATTERN_COVERS_CASE;
}

bool BSAnalyzer::collect_uncovered_tagged_union_cases(const BSParser::MatchNode *p_match, const BSParser::DataType &p_match_type, Vector<String> &r_uncovered) const {
	if (p_match == nullptr || p_match_type.enum_values.is_empty()) {
		return false;
	}

	HashSet<int64_t> covered_tags;
	bool null_covered = false;
	for (int b = 0; b < p_match->branches.size(); b++) {
		const BSParser::MatchBranchNode *branch = p_match->branches[b];
		if (branch == nullptr || branch->guard_body != nullptr) {
			continue; // Guard may fail; does not guarantee coverage.
		}
		for (int p = 0; p < branch->patterns.size(); p++) {
			const BSParser::PatternNode *pattern = branch->patterns[p];
			int64_t covered_tag = 0;
			switch (tagged_union_pattern_coverage(pattern, p_match_type, covered_tag)) {
				case TAGGED_UNION_PATTERN_COVERS_CASE:
					covered_tags.insert(covered_tag);
					break;
				case TAGGED_UNION_PATTERN_COVERS_NULL:
					null_covered = true;
					break;
				case TAGGED_UNION_PATTERN_COVERAGE_UNPROVABLE:
					return false; // Non-constant pattern: cannot prove coverage; bail out.
				case TAGGED_UNION_PATTERN_COVERS_NOTHING:
					break;
			}
		}
	}

	for (const KeyValue<StringName, int64_t> &E : p_match_type.enum_values) {
		if (!covered_tags.has(E.value)) {
			r_uncovered.push_back(String(E.key));
		}
	}
	if (p_match_type.is_nullable && !null_covered) {
		r_uncovered.push_back("null");
	}
	return true;
}

bool BSAnalyzer::node_terminates(const BSParser::Node *p_node) const {
	if (p_node == nullptr) {
		return false;
	}
	if (p_node->type == BSParser::Node::RETURN) {
		return true;
	}
	if (p_node->type == BSParser::Node::CALL && static_cast<const BSParser::CallNode *>(p_node)->is_noreturn) {
		return true;
	}
	if (p_node->type == BSParser::Node::SUITE) {
		return suite_has_return(static_cast<const BSParser::SuiteNode *>(p_node));
	}
	if (p_node->type == BSParser::Node::IF) {
		const BSParser::IfNode *if_node = static_cast<const BSParser::IfNode *>(p_node);
		return suite_has_return(if_node->true_block) && suite_has_return(if_node->false_block);
	}
	return false;
}

bool BSAnalyzer::suite_has_return(const BSParser::SuiteNode *p_suite) const {
	if (p_suite == nullptr) {
		return false;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		const BSParser::Node *statement = p_suite->statements[i];
		if (statement == nullptr) {
			continue;
		}
		if (node_terminates(statement)) {
			return true;
		}
		if (statement->type == BSParser::Node::IF) {
			const BSParser::IfNode *if_node = static_cast<const BSParser::IfNode *>(statement);
			if (suite_has_return(if_node->true_block) && suite_has_return(if_node->false_block)) {
				return true;
			}
		}
		if (statement->type == BSParser::Node::MATCH) {
			const BSParser::MatchNode *match_node = static_cast<const BSParser::MatchNode *>(statement);
			if (!match_node->covers_subject_domain || match_node->branches.is_empty()) {
				continue;
			}
			bool all_branches = true;
			for (int b = 0; b < match_node->branches.size(); b++) {
				if (match_node->branches[b] == nullptr || !suite_has_return(match_node->branches[b]->block)) {
					all_branches = false;
					break;
				}
			}
			if (all_branches) {
				return true;
			}
		}
		if (statement->type == BSParser::Node::SUITE && suite_has_return(static_cast<const BSParser::SuiteNode *>(statement))) {
			return true;
		}
	}
	return false;
}

bool BSAnalyzer::node_has_explicit_return(const BSParser::Node *p_node) const {
	if (p_node == nullptr) {
		return false;
	}
	if (p_node->type == BSParser::Node::RETURN) {
		return true;
	}
	if (p_node->type == BSParser::Node::SUITE) {
		return suite_has_explicit_return(static_cast<const BSParser::SuiteNode *>(p_node));
	}
	if (p_node->type == BSParser::Node::IF) {
		const BSParser::IfNode *if_node = static_cast<const BSParser::IfNode *>(p_node);
		return suite_has_explicit_return(if_node->true_block) || suite_has_explicit_return(if_node->false_block);
	}
	if (p_node->type == BSParser::Node::MATCH) {
		const BSParser::MatchNode *match_node = static_cast<const BSParser::MatchNode *>(p_node);
		for (int b = 0; b < match_node->branches.size(); b++) {
			if (match_node->branches[b] != nullptr && suite_has_explicit_return(match_node->branches[b]->block)) {
				return true;
			}
		}
	}
	if (p_node->type == BSParser::Node::WHILE) {
		return suite_has_explicit_return(static_cast<const BSParser::WhileNode *>(p_node)->loop);
	}
	if (p_node->type == BSParser::Node::FOR) {
		return suite_has_explicit_return(static_cast<const BSParser::ForNode *>(p_node)->loop);
	}
	return false;
}

bool BSAnalyzer::suite_has_explicit_return(const BSParser::SuiteNode *p_suite) const {
	if (p_suite == nullptr) {
		return false;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		if (node_has_explicit_return(p_suite->statements[i])) {
			return true;
		}
	}
	return false;
}

void BSAnalyzer::check_function_flow_finality(BSParser::FunctionNode *p_function) {
	if (p_function == nullptr || !p_function->has_body || p_function->body == nullptr) {
		return;
	}

	if (p_function->is_noreturn) {
		// Foundry SuiteExitState: has_return is recursive RETURN-only; noreturn calls set
		// always_terminates without has_return.
		if (suite_has_explicit_return(p_function->body)) {
			push_error(R"(A "@noreturn" function cannot return.)", p_function);
		} else if (!suite_has_return(p_function->body)) {
			push_error(R"(A "@noreturn" function cannot complete normally.)", p_function);
		}
	}

	const BSParser::DataType return_type = p_function->get_datatype();
	const bool expects_value = return_type.is_set() && !return_type.is_variant() &&
			!(return_type.kind == BSParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL);
	if (expects_value && !p_function->is_noreturn) {
		if (!suite_has_return(p_function->body)) {
			push_error(R"(Not all code paths return a value.)", p_function);
		}
	}

#ifdef DEBUG_ENABLED
	for (int i = 0; i + 1 < p_function->body->statements.size(); i++) {
		const BSParser::Node *statement = p_function->body->statements[i];
		if (statement != nullptr && (statement->type == BSParser::Node::RETURN || (statement->type == BSParser::Node::CALL && static_cast<const BSParser::CallNode *>(statement)->is_noreturn))) {
			const StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();
			Vector<String> symbols;
			symbols.push_back(String(function_name));
			push_warning(p_function->body->statements[i + 1], BSWarning::UNREACHABLE_CODE, symbols);
			break;
		}
	}
#endif
}

void BSAnalyzer::resolve_used_traits(BSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}
	if (p_class->resolved_trait_uses) {
		for (int i = 0; i < p_class->members.size(); i++) {
			if (p_class->members[i].type == BSParser::ClassNode::Member::CLASS) {
				resolve_used_traits(p_class->members[i].m_class);
			}
		}
		return;
	}
	if (p_class->failed_trait_uses) {
		return;
	}
	// Foundry resolve_trait_uses @ c9d5e35: fail() clears resolving and sets failed; never mark
	// resolved after a cycle or lookup miss, and never append a trait whose resolve failed.
	auto fail = [&]() {
		p_class->resolving_trait_uses = false;
		p_class->failed_trait_uses = true;
		p_class->resolved_trait_uses = false;
		p_class->resolved_traits.clear();
	};
	if (p_class->resolving_trait_uses) {
		push_error(vformat(R"(Could not resolve trait uses for "%s": Cyclic trait use.)",
						   p_class->identifier != nullptr ? String(p_class->identifier->name) : String("<anonymous>")),
				p_class);
		fail();
		return;
	}

	p_class->resolving_trait_uses = true;
	p_class->resolved_traits.clear();

	auto append_trait_unique = [](Vector<BSParser::ClassNode *> &r_traits, BSParser::ClassNode *p_trait) {
		if (p_trait == nullptr) {
			return;
		}
		for (int i = 0; i < r_traits.size(); i++) {
			if (r_traits[i] == p_trait) {
				return;
			}
		}
		r_traits.push_back(p_trait);
	};

	auto find_local_trait = [](BSParser::ClassNode *p_owner, const String &p_name) -> BSParser::ClassNode * {
		for (BSParser::ClassNode *scope = p_owner; scope != nullptr; scope = scope->outer) {
			if (!scope->has_member(StringName(p_name))) {
				continue;
			}
			const BSParser::ClassNode::Member member = scope->get_member(StringName(p_name));
			if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr && member.m_class->is_trait) {
				return member.m_class;
			}
		}
		return nullptr;
	};

	for (int i = 0; i < p_class->used_traits.size(); i++) {
		BSParser::ClassNode::TraitUse &use = p_class->used_traits.write[i];
		const String name = use.to_string();
		if (name.is_empty()) {
			fail();
			return;
		}
		if (!use.type_arguments.is_empty()) {
			push_error("Generic trait specialization is not available until M5.", p_class);
			fail();
			return;
		}

		BSParser::ClassNode *trait = use.resolved_trait;
		if (trait == nullptr) {
			trait = find_local_trait(p_class, name);
		}
		if (trait == nullptr) {
			BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
			BSDeclarationRecord record;
			bool found = false;
			if (language != nullptr) {
				found = language->try_resolve_declaration(name, record);
				if (!found && !p_class->namespace_name.is_empty()) {
					found = language->try_resolve_declaration(p_class->namespace_name + String(".") + name, record);
				}
				if (!found) {
					for (int j = 0; j < p_class->imports.size(); j++) {
						found = language->try_resolve_declaration(p_class->imports[j] + String(".") + name, record);
						if (found) {
							break;
						}
					}
				}
			}
			if (!found) {
				push_error(vformat(R"(Could not find trait "%s".)", name), p_class);
				fail();
				return;
			}
			if (record.kind != BSDeclarationKind::TRAIT) {
				push_error(vformat(R"("%s" is not a trait.)", name), p_class);
				fail();
				return;
			}
			Error err = OK;
			Ref<BSParserRef> trait_ref = BSCache::get_parser(record.path, BSParserRef::INTERFACE_SOLVED, err, parser != nullptr ? parser->script_path : String());
			if (trait_ref.is_null() || err != OK || trait_ref->get_parser() == nullptr || trait_ref->get_parser()->get_tree() == nullptr) {
				push_error(vformat(R"(Could not resolve trait "%s".)", name), p_class);
				fail();
				return;
			}
			trait = trait_ref->get_parser()->get_tree();
			if (trait == nullptr || !trait->is_trait) {
				push_error(vformat(R"("%s" is not a trait.)", name), p_class);
				fail();
				return;
			}
		} else if (!trait->is_trait) {
			push_error(vformat(R"("%s" is not a trait.)", name), p_class);
			fail();
			return;
		}

		use.resolved_trait = trait;
		resolve_used_traits(trait);
		if (trait->failed_trait_uses || (!trait->resolved_trait_uses && trait->resolving_trait_uses)) {
			// Nested cycle leaves failed_trait_uses; never append a half-resolved trait.
			fail();
			return;
		}
		if (!trait->resolved_trait_uses) {
			fail();
			return;
		}
		append_trait_unique(p_class->resolved_traits, trait);
		for (int t = 0; t < trait->resolved_traits.size(); t++) {
			append_trait_unique(p_class->resolved_traits, trait->resolved_traits[t]);
		}
	}

	p_class->resolving_trait_uses = false;
	p_class->resolved_trait_uses = true;
	p_class->failed_trait_uses = false;

	for (int i = 0; i < p_class->members.size(); i++) {
		if (p_class->members[i].type == BSParser::ClassNode::Member::CLASS) {
			resolve_used_traits(p_class->members[i].m_class);
		}
	}
}

BSParser::DataType BSAnalyzer::resolve_named_type_in_scope(const StringName &p_name, BSParser::Node *p_source) {
	const String qualified = p_name;
	BSParser::ClassNode *head = parser != nullptr ? parser->get_tree() : nullptr;
	BSParser::DataType indexed = resolve_named_type(qualified, p_source);
	if (indexed.is_variant() && head != nullptr && !head->namespace_name.is_empty()) {
		indexed = resolve_named_type(head->namespace_name + String(".") + qualified, p_source);
	}
	if (indexed.is_variant() && head != nullptr) {
		for (const String &import : head->imports) {
			indexed = resolve_named_type(import + String(".") + qualified, p_source);
			if (!indexed.is_variant()) {
				break;
			}
		}
	}
	return indexed;
}

BSParser::DataType BSAnalyzer::resolve_named_type(const String &p_qualified, BSParser::Node *p_source) {
	BSParser::DataType result;
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	BSDeclarationRecord record;
	if (language != nullptr && language->try_resolve_declaration(p_qualified, record)) {
		switch (record.kind) {
			case BSDeclarationKind::ENUM:
				result.kind = BSParser::DataType::ENUM;
				result.enum_type = StringName(record.qualified_name);
				result.builtin_type = Variant::INT;
				break;
			case BSDeclarationKind::TUPLE:
				result.kind = BSParser::DataType::TUPLE;
				result.builtin_type = Variant::ARRAY;
				break;
			case BSDeclarationKind::TRAIT:
			case BSDeclarationKind::CLASS:
			case BSDeclarationKind::GENERIC_CLASS:
				result.kind = BSParser::DataType::CLASS;
				result.script_path = record.path;
				result.native_type = StringName(record.base_type);
				result.builtin_type = Variant::OBJECT;
				break;
			default:
				result.kind = BSParser::DataType::VARIANT;
				break;
		}
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}
	if (ScriptServer::is_global_class(StringName(p_qualified))) {
		result.kind = BSParser::DataType::CLASS;
		result.script_path = ScriptServer::get_global_class_path(StringName(p_qualified));
		result.native_type = ScriptServer::get_global_class_native_base(StringName(p_qualified));
		result.builtin_type = Variant::OBJECT;
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}
	(void)p_source;
	result.kind = BSParser::DataType::VARIANT;
	return result;
}

Error BSAnalyzer::run_phase_body_expression_callable_signal() {
	analyze_class_body(parser->get_tree());
	// Foundry @ c9d5e35: shorthands no consumer qualified sat in a no-expected-type position.
	report_unqualified_contextual_enum_cases();
	mark_phase(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error BSAnalyzer::run_phase_flow_finality() {
	BSParser::ClassNode *head = parser->get_tree();
	if (head != nullptr) {
		// Foundry order @ c9d5e35: member, static, then local finals.
		flow_finality.check_final_member_assignments(head);
		flow_finality.check_final_static_assignments(head);
		flow_finality.check_final_local_assignments(head);
		for (int i = 0; i < head->members.size(); i++) {
			const BSParser::ClassNode::Member &member = head->members[i];
			if (member.type == BSParser::ClassNode::Member::FUNCTION) {
				check_function_flow_finality(member.function);
			}
		}
		// Foundry FLOW_FINALITY_INVARIANTS: abstract trait requirements after body.
		validate_trait_requirements(head);
	}
	mark_phase(AnalyzerPhase::FLOW_FINALITY_INVARIANTS);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error BSAnalyzer::run_phase_conformance_witness_body() {
	resolve_conformance_bodies(parser->get_tree());
	// Foundry @ c9d5e35: witness bodies need their own unqualified-shorthand sweep.
	report_unqualified_contextual_enum_cases();
	mark_phase(AnalyzerPhase::CONFORMANCE_WITNESS_BODY);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error BSAnalyzer::resolve_inheritance() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	const BSConformanceRegistry::ScopedVisibility conformance_scope(&conformance_visibility);
	Error err = run_phase_preflight();
	if (err != OK) {
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_inheritance_resolution();
	if (err != OK) {
		commit_or_remove_declaration(false);
	}
	return err;
}

Error BSAnalyzer::resolve_interface() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	const BSConformanceRegistry::ScopedVisibility conformance_scope(&conformance_visibility);
	Error err = run_phase_interface_and_member_surface();
	if (err != OK) {
		commit_or_remove_declaration(false);
	}
	return err;
}

Error BSAnalyzer::resolve_body() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	const BSConformanceRegistry::ScopedVisibility conformance_scope(&conformance_visibility);
	Error err = run_phase_body_expression_callable_signal();
	if (err != OK) {
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_flow_finality();
	if (err != OK) {
		// Foundry residual #60: body already queued pending warnings (e.g. NON_EXHAUSTIVE_MATCH);
		// flush them even when flow-finality exits early (return-typed incomplete matches).
		run_phase_finalize();
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_conformance_witness_body();
	if (err != OK) {
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_finalize();
	const bool success = err == OK || errors_are_only_m5_deferred();
	commit_or_remove_declaration(success);
	return err;
}

Error BSAnalyzer::analyze() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	const BSConformanceRegistry::ScopedVisibility conformance_scope(&conformance_visibility);
	Error err = run_phase_preflight();
	if (err != OK) {
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_inheritance_resolution();
	if (err != OK) {
		// Still collect interface diagnostics, matching Foundry's analyze() policy.
		run_phase_interface_and_member_surface();
		commit_or_remove_declaration(false);
		return err;
	}
	run_phase_interface_and_member_surface();
	err = run_phase_body_expression_callable_signal();
	if (err != OK && !errors_are_only_m5_deferred()) {
		commit_or_remove_declaration(false);
		return err;
	}
	Error flow_err = run_phase_flow_finality();
	if (flow_err != OK && !errors_are_only_m5_deferred()) {
		// Foundry residual #60: flush pending warnings even when flow-finality exits early
		// (latent with return-typed incomplete tagged-union matches).
		run_phase_finalize();
		commit_or_remove_declaration(false);
		return flow_err;
	}
	Error witness_err = run_phase_conformance_witness_body();
	if (witness_err != OK && !errors_are_only_m5_deferred()) {
		commit_or_remove_declaration(false);
		return witness_err;
	}
	err = run_phase_finalize();
	const bool success = (err == OK && parser->get_errors().is_empty()) || errors_are_only_m5_deferred();
	commit_or_remove_declaration(success);
	return success && !errors_are_only_m5_deferred() ? OK : (parser->get_errors().is_empty() ? err : ERR_PARSE_ERROR);
}

void BSAnalyzer::commit_or_remove_declaration(bool p_success) {
	if (!update_declaration_index) {
		return;
	}
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language == nullptr || parser == nullptr) {
		return;
	}
	const String path = BaristaScript::canonicalize_path(parser->script_path);
	if (path.is_empty() || !path.begins_with("res://")) {
		return;
	}
	const uint64_t token = language->claim_declaration_refresh(path);
	if (!p_success) {
		language->remove_declaration_path(path, token);
		return;
	}
	BSParser::ClassNode *head = parser->get_tree();
	if (head == nullptr) {
		language->remove_declaration_path(path, token);
		return;
	}
	const String source = BSCache::get_source_code(path);
	BSDeclarationRecord record;
	record.path = path;
	record.source_digest = BSDeclarationIndex::compute_source_digest(source);
	record.namespace_name = head->namespace_name;
	record.qualified_name = head->qualified_global_name;
	if (head->is_trait) {
		record.kind = BSDeclarationKind::TRAIT;
	} else if (head->is_enum_file) {
		record.kind = BSDeclarationKind::ENUM;
	} else if (head->is_tuple_file) {
		record.kind = BSDeclarationKind::TUPLE;
	} else if (!head->type_parameters.is_empty()) {
		record.kind = BSDeclarationKind::GENERIC_CLASS;
	} else if (head->identifier != nullptr) {
		record.kind = BSDeclarationKind::CLASS;
	} else {
		record.kind = BSDeclarationKind::NONE;
	}
	record.base_type = String(head->base_type.native_type);
	record.is_abstract = head->is_abstract || !bs_declaration_kind_is_instantiable(record.kind);
	record.is_tool = false;
	record.icon_path = head->icon_path;
	record.declares_retroactive_conformances = !head->conformances.is_empty();
	for (int i = 0; i < head->annotation_declarations.size(); i++) {
		if (head->annotation_declarations[i] != nullptr && head->annotation_declarations[i]->identifier != nullptr) {
			const String annotation_name = head->annotation_declarations[i]->qualified_name.is_empty()
					? String(head->annotation_declarations[i]->identifier->name)
					: head->annotation_declarations[i]->qualified_name;
			record.global_annotations.push_back(annotation_name);
		}
	}
	language->commit_declaration_record(token, record);
	ScriptServer::bump_global_class_cache_version();
}

bool bs_source_analyzes(const String &p_source, const String &p_path) {
	BSParser parser;
	BSAnalyzer analyzer(&parser);
	Error err = parser.parse(p_source, p_path, false);
	if (err != OK || !parser.get_errors().is_empty()) {
		return false;
	}
	err = analyzer.analyze();
	return err == OK && parser.get_errors().is_empty();
}

} // namespace barista_script
