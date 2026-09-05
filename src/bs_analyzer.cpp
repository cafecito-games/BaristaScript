/**************************************************************************/
/*  bs_analyzer.cpp                                                       */
/*                                                                        */
/*  M3 analyzer port (issue #43/#57/#60) @ Foundry c9d5e35. Inheritance,  */
/*  interface, body fold (#49), declaration commit (#52/#58), call/match/ */
/*  flow (#61), local/member/static final definite assignment (#60 TU),   */
/*  CallSiteValidationContext MethodInfo / signal emit / named-arg /      */
/*  connect-callable (#60 call TU),                                       */
/*  resolved_traits + trait-member lookup for flattening finality,        */
/*  unused private/signal surface, trait requirement / conformance        */
/*  witness starter (#60 conformance TU).                                 */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_builtin_sources.h"
#include "bs_cache.h"
#include "bs_declaration_index.h"
#include "bs_global_class.h"
#include "bs_native_db.h"
#include "bs_script_server.h"
#include "bs_warning.h"

namespace barista_script {

namespace {

String _operator_name(Variant::Operator p_op) {
	switch (p_op) {
		case Variant::OP_ADD:
			return "+";
		case Variant::OP_SUBTRACT:
			return "-";
		case Variant::OP_MULTIPLY:
			return "*";
		case Variant::OP_DIVIDE:
			return "/";
		case Variant::OP_MODULE:
			return "%";
		case Variant::OP_POWER:
			return "**";
		case Variant::OP_NEGATE:
			return "-";
		case Variant::OP_POSITIVE:
			return "+";
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

String &BSAnalyzer::bootstrap_root_storage() {
	static String *root = nullptr;
	if (root == nullptr) {
		root = memnew(String);
	}
	return *root;
}

BSAnalyzer::BSAnalyzer(BSParser *p_parser) :
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
	parser->push_error(p_message, p_origin);
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
	for (const BSParser::ParserError &error : parser->get_errors()) {
		if (!error.message.contains("not available until M5")) {
			return false;
		}
	}
	return true;
}

BSParser::FunctionNode *BSAnalyzer::find_class_function(BSParser::ClassNode *p_class, const StringName &p_name) const {
	if (p_class == nullptr || p_name == StringName()) {
		return nullptr;
	}
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const BSParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != BSParser::ClassNode::Member::FUNCTION) {
		return nullptr;
	}
	return member.function;
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
	mark_phase(AnalyzerPhase::PREFLIGHT);
	mark_phase(AnalyzerPhase::DEPENDENCY_PARSE_AVAILABILITY);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

void BSAnalyzer::resolve_class_inheritance(BSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::CLASS) {
			resolve_class_inheritance(member.m_class);
		}
	}

	if (!p_class->extends_used) {
		BSParser::DataType base;
		base.kind = BSParser::DataType::NATIVE;
		base.native_type = SNAME("RefCounted");
		base.builtin_type = Variant::OBJECT;
		base.type_source = BSParser::DataType::ANNOTATED_INFERRED;
		p_class->base_type = base;
		return;
	}

	if (!p_class->extends_path.is_empty()) {
		String path = p_class->extends_path.strip_edges();
		if (path.is_relative_path() && !parser->script_path.is_empty()) {
			path = parser->script_path.get_base_dir().path_join(path).simplify_path();
		}
		path = BaristaScript::canonicalize_path(path);
		if (!is_bootstrap_path_allowed(path) && path.begins_with("res://")) {
			// Explicit out-of-root import diagnostic (#52): only when the path is outside the bootstrap root.
			push_error(vformat(R"(Cannot depend on "%s": path is outside the bootstrap allowed dependency root.)", path), p_class);
		}
		Error err = OK;
		// raise_mutex is recursive so A→B→A re-enters the still-raising path without deadlocking.
		Ref<BSParserRef> base_ref = BSCache::get_parser(path, BSParserRef::INHERITANCE_SOLVED, err, parser->script_path);
		if (base_ref.is_null() || err != OK || base_ref->get_parser() == nullptr || base_ref->get_parser()->get_tree() == nullptr) {
			push_error(vformat(R"(Could not resolve base script "%s".)", path), p_class);
			return;
		}
		BSParser::ClassNode *base_class = base_ref->get_parser()->get_tree();
		BSParser::DataType base;
		base.kind = BSParser::DataType::CLASS;
		base.class_type = base_class;
		base.script_path = path;
		base.native_type = base_class->base_type.native_type;
		base.builtin_type = Variant::OBJECT;
		base.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		p_class->base_type = base;
		return;
	}

	if (p_class->extends.is_empty()) {
		push_error("Extends used without a base type.", p_class);
		return;
	}

	// D7: native and GDScript names remain flat — only the first identifier is consulted for natives.
	const StringName first = p_class->extends[0]->name;
	if (p_class->extends.size() == 1 && ClassDB::class_exists(first)) {
		BSParser::DataType base;
		base.kind = BSParser::DataType::NATIVE;
		base.native_type = first;
		base.builtin_type = Variant::OBJECT;
		base.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		p_class->base_type = base;
		return;
	}

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
		Ref<BSParserRef> base_ref = BSCache::get_parser(path, BSParserRef::INHERITANCE_SOLVED, err, parser->script_path);
		if (base_ref.is_null() || err != OK || base_ref->get_parser() == nullptr) {
			push_error(vformat(R"(Could not resolve global class base "%s".)", qualified), p_class);
			return;
		}
		BSParser::ClassNode *base_class = base_ref->get_parser()->get_tree();
		BSParser::DataType base;
		base.kind = BSParser::DataType::CLASS;
		base.class_type = base_class;
		base.script_path = path;
		base.native_type = ScriptServer::get_global_class_native_base(StringName(qualified));
		if (base.native_type == StringName() && base_class != nullptr) {
			base.native_type = base_class->base_type.native_type;
		}
		base.builtin_type = Variant::OBJECT;
		base.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		p_class->base_type = base;
		return;
	}

	push_error(vformat(R"(Could not find base class "%s".)", qualified), p_class->extends[0]);
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

BSParser::DataType BSAnalyzer::datatype_from_type_node(BSParser::TypeNode *p_type_node) {
	BSParser::DataType result;
	if (p_type_node == nullptr) {
		result.kind = BSParser::DataType::VARIANT;
		return result;
	}
	result.is_nullable = p_type_node->is_nullable;
	if (p_type_node->is_union) {
		result.kind = BSParser::DataType::UNION;
		for (int i = 0; i < p_type_node->union_member_types.size(); i++) {
			result.union_members.push_back(datatype_from_type_node(p_type_node->union_member_types[i]));
		}
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
		} else if (ClassDB::class_exists(name)) {
			result.kind = BSParser::DataType::NATIVE;
			result.native_type = name;
			result.builtin_type = Variant::OBJECT;
		} else {
			String qualified = String(name);
			BSParser::ClassNode *head = parser != nullptr ? parser->get_tree() : nullptr;
			BSParser::DataType indexed = resolve_named_type(qualified, p_type_node);
			if (indexed.kind == BSParser::DataType::VARIANT && head != nullptr && !head->namespace_name.is_empty()) {
				indexed = resolve_named_type(head->namespace_name + String(".") + qualified, p_type_node);
			}
			if (indexed.kind == BSParser::DataType::VARIANT && head != nullptr) {
				for (int i = 0; i < head->imports.size(); i++) {
					indexed = resolve_named_type(head->imports[i] + String(".") + qualified, p_type_node);
					if (indexed.kind != BSParser::DataType::VARIANT) {
						break;
					}
				}
			}
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
	BSParser::DataType indexed = resolve_named_type(qualified, p_type_node);
	if (indexed.kind != BSParser::DataType::VARIANT) {
		return indexed;
	}
	push_error(vformat(R"(Could not find type "%s".)", qualified), p_type_node->type_chain[0]);
	result.kind = BSParser::DataType::VARIANT;
	return result;
}

void BSAnalyzer::analyze_class_interface(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->resolved_interface) {
		return;
	}
	p_class->resolved_interface = true;

	BSParser::ClassNode *previous_class = current_class;
	current_class = p_class;

	HashSet<StringName> seen;
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		const StringName name = StringName(member.get_name());
		if (name != StringName()) {
			if (seen.has(name)) {
				push_error(vformat(R"(Member "%s" is declared more than once.)", name), member.get_source_node());
			}
			seen.insert(name);
		}
		switch (member.type) {
			case BSParser::ClassNode::Member::CLASS:
				analyze_class_interface(member.m_class);
				break;
			case BSParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					resolve_function_signature_in_class(member.function, p_class);
					if (!member.function->type_parameters.is_empty()) {
						push_error("Generic function specialization is not available until M5.", member.function);
					}
				}
				break;
			case BSParser::ClassNode::Member::VARIABLE:
				if (member.variable != nullptr && member.variable->datatype_specifier != nullptr) {
					member.variable->set_datatype(datatype_from_type_node(member.variable->datatype_specifier));
				}
				break;
			case BSParser::ClassNode::Member::SIGNAL:
				if (member.signal != nullptr) {
					// Foundry fs_analyzer_surface.cpp @ c9d5e35: build MethodInfo + rich parameter
					// types so CallSiteValidationContext can validate emit / emit_signal.
					MethodInfo mi = MethodInfo(member.signal->identifier != nullptr ? member.signal->identifier->name : StringName());
					BSParser::DataType signal_type;
					signal_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
					signal_type.kind = BSParser::DataType::BUILTIN;
					signal_type.builtin_type = Variant::SIGNAL;
					signal_type.is_constant = true;
					signal_type.has_method_signature = true;
					signal_type.has_explicit_method_signature = true;
					for (int j = 0; j < member.signal->parameters.size(); j++) {
						BSParser::ParameterNode *param = member.signal->parameters[j];
						if (param == nullptr) {
							continue;
						}
						if (param->datatype_specifier != nullptr) {
							param->set_datatype(datatype_from_type_node(param->datatype_specifier));
						}
						const BSParser::DataType param_type = param->get_datatype();
						signal_type.method_parameter_types.push_back(param_type);
						if (param->identifier != nullptr) {
							mi.arguments.push_back(param_type.to_property_info(param->identifier->name));
						}
					}
					signal_type.method_info = mi;
					member.signal->method_info = mi;
					member.signal->set_datatype(signal_type);
				}
				break;
			default:
				break;
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
	// Foundry TRAIT_CONFORMANCE: registration / extend targets only.
	// Abstract-method requirements belong in FLOW_FINALITY_INVARIANTS.
	resolve_conformances(parser->get_tree());
	mark_phase(AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
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
	if (!p_unary_op->operand->is_constant) {
		BSParser::DataType operand_type = p_unary_op->operand->get_datatype();
		p_unary_op->set_datatype(operand_type);
		return;
	}
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
	if (!(p_binary_op->left_operand->is_constant && p_binary_op->right_operand->is_constant)) {
		BSParser::DataType left = p_binary_op->left_operand->get_datatype();
		BSParser::DataType right = p_binary_op->right_operand->get_datatype();
		if (left.is_set() && right.is_set() && left.kind == BSParser::DataType::BUILTIN && right.kind == BSParser::DataType::BUILTIN) {
			if (left.builtin_type == Variant::INT && right.builtin_type == Variant::INT) {
				BSParser::DataType result = type_from_variant(0);
				result.is_constant = false;
				p_binary_op->set_datatype(result);
#ifdef DEBUG_ENABLED
				if (p_binary_op->variant_op == Variant::OP_DIVIDE) {
					push_warning(p_binary_op, BSWarning::INTEGER_DIVISION);
				}
#endif
			} else if (left.builtin_type == Variant::FLOAT || right.builtin_type == Variant::FLOAT) {
				BSParser::DataType result = type_from_variant(0.0);
				result.is_constant = false;
				p_binary_op->set_datatype(result);
			}
		}
		return;
	}

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
#ifdef DEBUG_ENABLED
	if (p_binary_op->variant_op == Variant::OP_DIVIDE &&
			p_binary_op->left_operand->reduced_value.get_type() == Variant::INT &&
			p_binary_op->right_operand->reduced_value.get_type() == Variant::INT) {
		push_warning(p_binary_op, BSWarning::INTEGER_DIVISION);
	}
#endif
	p_binary_op->set_datatype(type_from_variant(p_binary_op->reduced_value));
}

void BSAnalyzer::reduce_identifier(BSParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
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
				return;
			}
		}
	}
	if (current_class != nullptr && current_class->has_member(p_identifier->name)) {
		const BSParser::ClassNode::Member member = current_class->get_member(p_identifier->name);
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
			p_identifier->source = member.variable->is_static ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE;
			p_identifier->variable_source = member.variable;
			member.variable->usages++;
			p_identifier->set_datatype(member.variable->get_datatype());
			return;
		}
		if (member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr) {
			p_identifier->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
			p_identifier->constant_source = member.constant;
			member.constant->usages++;
			p_identifier->set_datatype(member.constant->get_datatype());
			return;
		}
		if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
			p_identifier->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
			p_identifier->signal_source = member.signal;
			member.signal->usages++;
			p_identifier->set_datatype(call_site_validation.explicit_signal_type_from_node(member.signal, current_class->get_datatype(), current_class));
			return;
		}
		if (member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr) {
			// Bare function references form Callables so signal.connect(handler) can check signatures
			// (Foundry reduce_identifier_from_base MEMBER_FUNCTION @ c9d5e35).
			p_identifier->source = BSParser::IdentifierNode::MEMBER_FUNCTION;
			p_identifier->function_source = member.function;
			p_identifier->function_source_is_static = member.function->is_static;
			p_identifier->set_datatype(call_site_validation.callable_type_from_function(member.function));
			return;
		}
	}
	// Foundry surface: flattened trait members are visible on the implementer (#60).
	if (current_class != nullptr) {
		for (int t = 0; t < current_class->resolved_traits.size(); t++) {
			BSParser::ClassNode *trait = current_class->resolved_traits[t];
			if (trait == nullptr || !trait->has_member(p_identifier->name)) {
				continue;
			}
			const BSParser::ClassNode::Member member = trait->get_member(p_identifier->name);
			if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
				p_identifier->source = member.variable->is_static ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE;
				p_identifier->variable_source = member.variable;
				member.variable->usages++;
				p_identifier->set_datatype(member.variable->get_datatype());
				return;
			}
			if (member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr) {
				p_identifier->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
				p_identifier->constant_source = member.constant;
				member.constant->usages++;
				p_identifier->set_datatype(member.constant->get_datatype());
				return;
			}
			if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
				p_identifier->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
				p_identifier->signal_source = member.signal;
				member.signal->usages++;
				p_identifier->set_datatype(call_site_validation.explicit_signal_type_from_node(member.signal, current_class->get_datatype(), current_class));
				return;
			}
		}
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_identifier->set_datatype(type);
}

void BSAnalyzer::validate_local_call(BSParser::CallNode *p_call, BSParser::FunctionNode *p_callee) {
	if (p_call == nullptr || p_callee == nullptr) {
		return;
	}
	// Named arguments rewrite into canonical positional order before arity/type checks
	// (Foundry CallSiteValidationContext::canonicalize_named_call_arguments @ c9d5e35).
	if (!call_site_validation.canonicalize_named_call_arguments(p_call, p_callee)) {
		p_call->set_datatype(p_callee->get_datatype());
		return;
	}
	List<BSParser::DataType> par_types;
	int default_arg_count = 0;
	for (int i = 0; i < p_callee->parameters.size(); i++) {
		BSParser::ParameterNode *parameter = p_callee->parameters[i];
		if (parameter == nullptr) {
			par_types.push_back(BSParser::DataType());
			continue;
		}
		par_types.push_back(parameter->get_datatype());
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
		rest_type = &rest_storage;
	}
	call_site_validation.validate_call_arg(par_types, default_arg_count, p_callee->is_vararg(), p_call, Vector<int>(), 0, rest_type);
	p_call->set_datatype(p_callee->get_datatype());
}

void BSAnalyzer::reduce_call(BSParser::CallNode *p_call) {
	if (p_call == nullptr) {
		return;
	}
	for (int i = 0; i < p_call->arguments.size(); i++) {
		reduce_expression(p_call->arguments[i]);
	}

	// Attribute call: `receiver.method(...)` — signal.emit and member-method shapes.
	if (p_call->get_callee_type() == BSParser::Node::SUBSCRIPT) {
		BSParser::SubscriptNode *subscript = static_cast<BSParser::SubscriptNode *>(p_call->callee);
		if (subscript != nullptr && subscript->is_attribute && subscript->attribute != nullptr) {
			reduce_expression(subscript->base);
			const bool is_self = subscript->base != nullptr && subscript->base->type == BSParser::Node::SELF;
			if (p_call->function_name == StringName()) {
				p_call->function_name = subscript->attribute->name;
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

			// Native MethodInfo path on a typed native / class receiver (Foundry validate_call_arg(MethodInfo)).
			if (subscript->base != nullptr && p_call->function_name != StringName()) {
				const BSParser::DataType base_type = subscript->base->get_datatype();
				StringName native_type;
				if (base_type.kind == BSParser::DataType::NATIVE) {
					native_type = base_type.native_type;
				} else if (base_type.kind == BSParser::DataType::CLASS && base_type.native_type != StringName()) {
					native_type = base_type.native_type;
				} else if (is_self && current_class != nullptr && current_class->base_type.native_type != StringName()) {
					native_type = current_class->base_type.native_type;
				}
				if (native_type != StringName()) {
					MethodInfo method_info;
					if (BSNativeDB::get_method_info(native_type, p_call->function_name, &method_info)) {
						call_site_validation.reject_named_call_arguments(p_call);
						call_site_validation.validate_call_arg(method_info, p_call);
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
			}
		}
	}

	if (current_class != nullptr) {
		StringName fname = p_call->function_name;
		if (fname == StringName() && p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER) {
			fname = static_cast<BSParser::IdentifierNode *>(p_call->callee)->name;
		}
		// Same-class bare call: callee is the identifier itself (or null for some super forms).
		const bool local_shape = p_call->callee == nullptr || p_call->callee->type == BSParser::Node::IDENTIFIER;
		if (local_shape && fname != StringName()) {
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
		}
	}
	// Foundry marks `push_fatal` as noreturn at call sites (fs_analyzer.cpp @ c9d5e35).
	if (p_call->function_name == SNAME("push_fatal") ||
			(p_call->callee != nullptr && p_call->callee->type == BSParser::Node::IDENTIFIER &&
					static_cast<BSParser::IdentifierNode *>(p_call->callee)->name == SNAME("push_fatal"))) {
		p_call->is_noreturn = true;
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_call->set_datatype(type);
}

void BSAnalyzer::reduce_subscript(BSParser::SubscriptNode *p_subscript) {
	if (p_subscript == nullptr) {
		return;
	}
	reduce_expression(p_subscript->base);
	if (p_subscript->is_attribute) {
		// Bind `self.<member>` and same-class `ClassName.<static>` so flow finality can see
		// MEMBER_VARIABLE / STATIC_VARIABLE on the attribute (Foundry resolve_subscript @ c9d5e35).
		if (p_subscript->attribute != nullptr && p_subscript->base != nullptr && current_class != nullptr &&
				current_class->has_member(p_subscript->attribute->name)) {
			const bool self_receiver = p_subscript->base->type == BSParser::Node::SELF;
			bool class_name_receiver = false;
			if (p_subscript->base->type == BSParser::Node::IDENTIFIER) {
				const BSParser::IdentifierNode *base_id = static_cast<const BSParser::IdentifierNode *>(p_subscript->base);
				const StringName class_name = current_class->identifier != nullptr ? current_class->identifier->name : StringName();
				const StringName global_name = current_class->get_global_name();
				class_name_receiver = base_id->name == class_name || (global_name != StringName() && base_id->name == global_name);
			}
			if (self_receiver || class_name_receiver) {
				const BSParser::ClassNode::Member member = current_class->get_member(p_subscript->attribute->name);
				if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
					if (class_name_receiver && !member.variable->is_static) {
						// ClassName.instance_member is not a legal static access; leave unbound.
					} else {
						p_subscript->attribute->source = member.variable->is_static ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE;
						p_subscript->attribute->variable_source = member.variable;
						member.variable->usages++;
						p_subscript->attribute->set_datatype(member.variable->get_datatype());
						p_subscript->set_datatype(member.variable->get_datatype());
						return;
					}
				}
				if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr && !class_name_receiver) {
					p_subscript->attribute->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
					p_subscript->attribute->signal_source = member.signal;
					member.signal->usages++;
					const BSParser::DataType signal_type = call_site_validation.explicit_signal_type_from_node(member.signal, current_class->get_datatype(), current_class);
					p_subscript->attribute->set_datatype(signal_type);
					p_subscript->set_datatype(signal_type);
					return;
				}
			}
		}
	} else {
		reduce_expression(p_subscript->index);
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

void BSAnalyzer::reduce_dictionary(BSParser::DictionaryNode *p_dictionary) {
	if (p_dictionary == nullptr) {
		return;
	}
	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		reduce_expression(p_dictionary->elements[i].key);
		reduce_expression(p_dictionary->elements[i].value);
	}
	BSParser::DataType type;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = Variant::DICTIONARY;
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

void BSAnalyzer::reduce_type_test(BSParser::TypeTestNode *p_type_test) {
	// Foundry reduce_type_test starter (@ c9d5e35): resolve the tested type so flow narrowing can
	// overlay it on locals/parameters. Contextual enum-case shorthand, case-bind payload typing,
	// constant folding, and exhausting-alternative diagnostics remain follow-up under #60.
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
	reduce_expression(p_type_test->operand);

	BSParser::DataType test_type = datatype_from_type_node(p_type_test->test_type);
	test_type.is_meta_type = false;
	if (test_type.is_union()) {
		push_error(vformat(R"(Cannot test against the type union "%s", because it has no runtime type. Test one of its alternatives instead.)", test_type.to_string()), p_type_test->test_type);
		test_type = BSParser::DataType();
	}
	p_type_test->test_datatype = test_type;

	if (!test_type.is_set()) {
		for (BSParser::IdentifierNode *bind : p_type_test->case_binds) {
			if (bind != nullptr) {
				BSParser::DataType bind_type;
				bind_type.kind = BSParser::DataType::VARIANT;
				bind_type.type_source = BSParser::DataType::INFERRED;
				bind->set_datatype(bind_type);
			}
		}
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

void BSAnalyzer::reduce_expression(BSParser::ExpressionNode *p_expression, bool p_is_root) {
	(void)p_is_root;
	if (p_expression == nullptr || p_expression->reduced) {
		return;
	}
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
		case BSParser::Node::CALL:
			reduce_call(static_cast<BSParser::CallNode *>(p_expression));
			break;
		case BSParser::Node::SUBSCRIPT:
			reduce_subscript(static_cast<BSParser::SubscriptNode *>(p_expression));
			break;
		case BSParser::Node::ARRAY:
			reduce_array(static_cast<BSParser::ArrayNode *>(p_expression));
			break;
		case BSParser::Node::DICTIONARY:
			reduce_dictionary(static_cast<BSParser::DictionaryNode *>(p_expression));
			break;
		case BSParser::Node::TERNARY_OPERATOR:
			reduce_ternary(static_cast<BSParser::TernaryOpNode *>(p_expression));
			break;
		case BSParser::Node::TYPE_TEST:
			reduce_type_test(static_cast<BSParser::TypeTestNode *>(p_expression));
			break;
		case BSParser::Node::SELF: {
			BSParser::SelfNode *self_node = static_cast<BSParser::SelfNode *>(p_expression);
			if (current_class != nullptr) {
				self_node->set_datatype(current_class->get_datatype());
			}
			self_node->reduced = true;
		} break;
		case BSParser::Node::ASSIGNMENT: {
			BSParser::AssignmentNode *assignment = static_cast<BSParser::AssignmentNode *>(p_expression);
			reduce_expression(assignment->assigned_value);
			reduce_expression(assignment->assignee);
			if (assignment->assignee != nullptr && assignment->assignee->type == BSParser::Node::IDENTIFIER) {
				BSParser::IdentifierNode *assignee = static_cast<BSParser::IdentifierNode *>(assignment->assignee);
				if (assignee->variable_source != nullptr) {
					assignee->variable_source->assignments++;
				}
			}
			// Foundry: assignment clears prior narrowing for the assignee (new value may not satisfy it).
			flow_finality.clear_flow_narrowing(assignment->assignee);
			if (assignment->assigned_value != nullptr) {
				assignment->set_datatype(assignment->assigned_value->get_datatype());
			}
		} break;
		default:
			break;
	}
	p_expression->reduced = true;
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
			if (declared.is_set() && !declared.is_variant() && variable->initializer != nullptr && variable->initializer->get_datatype().is_set()) {
				BSTypeCompatibility::Options options;
				options.allow_implicit_conversion = true;
				options.strict_dynamic = strict_dynamic_checks;
				options.strict_null = strict_null_checks;
				if (variable->initializer->is_constant) {
					options.constant_source_value = &variable->initializer->reduced_value;
				}
				if (!BSTypeCompatibility::check(declared, variable->initializer->get_datatype(), options).compatible) {
					push_error(vformat(R"(Cannot assign a value of type "%s" to a variable of type "%s".)",
									   variable->initializer->get_datatype().to_string(), declared.to_string()),
							variable);
				}
			}
		} break;
		case BSParser::Node::RETURN: {
			BSParser::ReturnNode *ret = static_cast<BSParser::ReturnNode *>(p_node);
			if (ret->return_value != nullptr) {
				reduce_expression(ret->return_value);
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
			analyze_suite(for_node->loop);
		} break;
		case BSParser::Node::MATCH: {
			BSParser::MatchNode *match_node = static_cast<BSParser::MatchNode *>(p_node);
			reduce_expression(match_node->test);
			for (int i = 0; i < match_node->branches.size(); i++) {
				if (match_node->branches[i] != nullptr) {
					analyze_suite(match_node->branches[i]->block);
				}
			}
			check_match_exhaustiveness(match_node);
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
	}
}

void BSAnalyzer::analyze_function_body(BSParser::FunctionNode *p_function) {
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
		FlowFinalityContext::FlowNarrowingScope flow_scope(flow_finality, true);
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

void BSAnalyzer::analyze_class_body(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->resolved_body) {
		return;
	}
	p_class->resolved_body = true;
	BSParser::ClassNode *previous = current_class;
	current_class = p_class;
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		switch (member.type) {
			case BSParser::ClassNode::Member::CLASS:
				analyze_class_body(member.m_class);
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
					if (member.constant->initializer != nullptr) {
						reduce_expression(member.constant->initializer);
						if (member.constant->initializer->is_constant) {
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
	current_class = previous;
}

void BSAnalyzer::check_match_exhaustiveness(BSParser::MatchNode *p_match) {
	if (p_match == nullptr || p_match->test == nullptr) {
		return;
	}
	p_match->covers_subject_domain = false;
	p_match->subject_domain_name = String();
	p_match->uncovered_domain_values = String();

	bool has_default = false;
	for (int i = 0; i < p_match->branches.size(); i++) {
		BSParser::MatchBranchNode *branch = p_match->branches[i];
		if (branch != nullptr && branch->has_wildcard) {
			has_default = true;
			break;
		}
	}
	if (has_default) {
		p_match->covers_subject_domain = true;
		return;
	}

	const BSParser::DataType match_type = p_match->test->get_datatype();
	if (match_type.kind == BSParser::DataType::BUILTIN && match_type.builtin_type == Variant::BOOL) {
		HashSet<bool> covered;
		for (int i = 0; i < p_match->branches.size(); i++) {
			BSParser::MatchBranchNode *branch = p_match->branches[i];
			if (branch == nullptr) {
				continue;
			}
			for (int p = 0; p < branch->patterns.size(); p++) {
				BSParser::PatternNode *pattern = branch->patterns[p];
				if (pattern == nullptr) {
					continue;
				}
				if (pattern->pattern_type == BSParser::PatternNode::PT_LITERAL && pattern->literal != nullptr &&
						pattern->literal->value.get_type() == Variant::BOOL) {
					covered.insert(bool(pattern->literal->value));
				} else if (pattern->pattern_type == BSParser::PatternNode::PT_EXPRESSION && pattern->expression != nullptr &&
						pattern->expression->is_constant && pattern->expression->reduced_value.get_type() == Variant::BOOL) {
					covered.insert(bool(pattern->expression->reduced_value));
				}
			}
		}
		Vector<String> unhandled;
		if (!covered.has(false)) {
			unhandled.push_back("false");
		}
		if (!covered.has(true)) {
			unhandled.push_back("true");
		}
		p_match->subject_domain_name = "bool";
		if (unhandled.is_empty()) {
			p_match->covers_subject_domain = true;
			return;
		}
		PackedStringArray uncovered_packed;
		for (int u = 0; u < unhandled.size(); u++) {
			uncovered_packed.push_back(unhandled[u]);
		}
		p_match->uncovered_domain_values = String(", ").join(uncovered_packed);
#ifdef DEBUG_ENABLED
		Vector<String> symbols;
		symbols.push_back("bool");
		symbols.push_back(p_match->uncovered_domain_values);
		push_warning(p_match, BSWarning::NON_EXHAUSTIVE_MATCH, symbols);
#endif
		return;
	}

#ifdef DEBUG_ENABLED
	push_warning(p_match, BSWarning::MATCH_WITHOUT_DEFAULT);
#endif
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
	mark_phase(AnalyzerPhase::CONFORMANCE_WITNESS_BODY);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error BSAnalyzer::run_phase_finalize() {
#ifdef DEBUG_ENABLED
	parser->apply_pending_warnings();
#endif
	mark_phase(AnalyzerPhase::FINAL_DIAGNOSTICS_AND_DEPENDENCIES);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error BSAnalyzer::resolve_inheritance() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
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
	Error err = run_phase_interface_and_member_surface();
	if (err != OK) {
		commit_or_remove_declaration(false);
	}
	return err;
}

Error BSAnalyzer::resolve_body() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	Error err = run_phase_body_expression_callable_signal();
	if (err != OK) {
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_flow_finality();
	if (err != OK) {
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
