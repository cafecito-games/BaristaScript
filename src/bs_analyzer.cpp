/**************************************************************************/
/*  bs_analyzer.cpp                                                       */
/*                                                                        */
/*  M3 analyzer port (issue #43) @ Foundry c9d5e35. Incremental green     */
/*  slice: inheritance, interface surface, body reduction/#49 folding,    */
/*  declaration commit (#52), validate wiring. Full Foundry phase depth   */
/*  remains follow-up work under epic #42.                                */
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
#include "bs_script_server.h"

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
		parser(p_parser) {
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

Error BSAnalyzer::run_phase_preflight() {
	ERR_FAIL_COND_V(parser == nullptr, ERR_BUG);
	if (!parser->get_errors().is_empty()) {
		return ERR_PARSE_ERROR;
	}
	mark_phase(AnalyzerPhase::PREFLIGHT);
	return OK;
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
		const String path = BaristaScript::canonicalize_path(p_class->extends_path);
		if (!is_bootstrap_path_allowed(path) && path.begins_with("res://")) {
			// Explicit out-of-root import diagnostic (#52): only when the path is outside the bootstrap root.
			push_error(vformat(R"(Cannot depend on "%s": path is outside the bootstrap allowed dependency root.)", path), p_class);
		}
		Error err = OK;
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
		result.kind = BSParser::DataType::VARIANT;
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
		if (name == SNAME("int")) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = Variant::INT;
		} else if (name == SNAME("float")) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = Variant::FLOAT;
		} else if (name == SNAME("bool")) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = Variant::BOOL;
		} else if (name == SNAME("String") || name == SNAME("string")) {
			result.kind = BSParser::DataType::BUILTIN;
			result.builtin_type = Variant::STRING;
		} else if (name == SNAME("Variant") || name == SNAME("void")) {
			result.kind = BSParser::DataType::VARIANT;
		} else if (ClassDB::class_exists(name)) {
			result.kind = BSParser::DataType::NATIVE;
			result.native_type = name;
			result.builtin_type = Variant::OBJECT;
		} else if (ScriptServer::is_global_class(name)) {
			result.kind = BSParser::DataType::CLASS;
			result.script_path = ScriptServer::get_global_class_path(name);
			result.native_type = ScriptServer::get_global_class_native_base(name);
			result.builtin_type = Variant::OBJECT;
		} else {
			push_error(vformat(R"(Could not find type "%s".)", name), p_type_node->type_chain[0]);
			result.kind = BSParser::DataType::VARIANT;
			return result;
		}
		result.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		return result;
	}

	push_error(vformat(R"(Could not find type "%s".)", name), p_type_node->type_chain[0]);
	result.kind = BSParser::DataType::VARIANT;
	return result;
}

void BSAnalyzer::analyze_class_interface(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->resolved_interface) {
		return;
	}
	p_class->resolved_interface = true;

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
				if (member.function != nullptr && member.function->return_type != nullptr) {
					BSParser::DataType return_type = member.function->get_datatype();
					resolve_datatype(return_type, member.function);
				}
				if (member.function != nullptr && !member.function->type_parameters.is_empty()) {
					push_error("Generic function specialization is not available until M5.", member.function);
				}
				break;
			case BSParser::ClassNode::Member::VARIABLE:
				if (member.variable != nullptr && member.variable->datatype_specifier != nullptr) {
					BSParser::DataType var_type = member.variable->get_datatype();
					resolve_datatype(var_type, member.variable);
				}
				break;
			default:
				break;
		}
	}
	if (!p_class->type_parameters.is_empty()) {
		push_error("Generic class specialization is not available until M5.", p_class);
	}
}

Error BSAnalyzer::run_phase_interface_and_member_surface() {
	analyze_class_interface(parser->get_tree());
	mark_phase(AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE);
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
	p_binary_op->set_datatype(type_from_variant(p_binary_op->reduced_value));
}

void BSAnalyzer::reduce_identifier(BSParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return;
	}
	// Local/member resolution is completed in later analyzer depth; unknown bare names fail closed
	// only when used as a call target or assignment source after interface resolution.
	BSParser::DataType type;
	type.kind = BSParser::DataType::VARIANT;
	p_identifier->set_datatype(type);
}

void BSAnalyzer::reduce_call(BSParser::CallNode *p_call) {
	if (p_call == nullptr) {
		return;
	}
	for (int i = 0; i < p_call->arguments.size(); i++) {
		reduce_expression(p_call->arguments[i]);
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
	if (!p_subscript->is_attribute) {
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
		case BSParser::Node::ASSIGNMENT: {
			BSParser::AssignmentNode *assignment = static_cast<BSParser::AssignmentNode *>(p_expression);
			reduce_expression(assignment->assigned_value);
			reduce_expression(assignment->assignee);
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
			BSParser::IfNode *if_node = static_cast<BSParser::IfNode *>(p_node);
			reduce_expression(if_node->condition);
			analyze_suite(if_node->true_block);
			analyze_suite(if_node->false_block);
		} break;
		case BSParser::Node::WHILE: {
			BSParser::WhileNode *while_node = static_cast<BSParser::WhileNode *>(p_node);
			reduce_expression(while_node->condition);
			analyze_suite(while_node->loop);
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
		} break;
		case BSParser::Node::ASSERT: {
			BSParser::AssertNode *assert_node = static_cast<BSParser::AssertNode *>(p_node);
			reduce_expression(assert_node->condition);
			reduce_expression(assert_node->message);
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
	if (!p_function->has_body) {
		if (!p_function->is_abstract) {
			push_error(vformat(R"(Function "%s" must have a body or be declared abstract.)", p_function->identifier != nullptr ? p_function->identifier->name : StringName()), p_function);
		}
		return;
	}
	analyze_suite(p_function->body);
}

void BSAnalyzer::analyze_class_body(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->resolved_body) {
		return;
	}
	p_class->resolved_body = true;
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
				if (member.variable != nullptr && member.variable->initializer != nullptr) {
					reduce_expression(member.variable->initializer);
				}
				break;
			case BSParser::ClassNode::Member::CONSTANT:
				if (member.constant != nullptr && member.constant->initializer != nullptr) {
					reduce_expression(member.constant->initializer);
					if (member.constant->initializer->is_constant) {
						member.constant->initializer->set_datatype(type_from_variant(member.constant->initializer->reduced_value));
					}
				}
				break;
			default:
				break;
		}
	}
}

Error BSAnalyzer::run_phase_body_expression_callable_signal() {
	analyze_class_body(parser->get_tree());
	mark_phase(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL);
	mark_phase(AnalyzerPhase::FLOW_FINALITY_INVARIANTS);
	mark_phase(AnalyzerPhase::CONFORMANCE_WITNESS_BODY);
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

Error BSAnalyzer::run_phase_finalize() {
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
	err = run_phase_finalize();
	commit_or_remove_declaration(err == OK);
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
	if (err != OK) {
		commit_or_remove_declaration(false);
		return err;
	}
	err = run_phase_finalize();
	commit_or_remove_declaration(err == OK && parser->get_errors().is_empty());
	return err;
}

void BSAnalyzer::commit_or_remove_declaration(bool p_success) {
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
			record.global_annotations.push_back(String(head->annotation_declarations[i]->identifier->name));
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
