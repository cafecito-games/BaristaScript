/**************************************************************************/
/*  bs_analyzer_call_validation.cpp                                       */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_analyzer_call_        */
/*  validation.cpp` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* ->    */
/*  BS*; engine contact through bs_platform.h. MethodInfo / typed call    */
/*  arity+types and signal emit / emit_signal validation for #60.         */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "bs_native_db.h"

namespace barista_script {

static BSParser::DataType make_signal_type(const MethodInfo &p_info) {
	BSParser::DataType type;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = Variant::SIGNAL;
	type.is_constant = true;
	type.method_info = p_info;
	type.has_method_signature = true;
	return type;
}

static bool _method_signature_accepts_argument_count(int p_argument_count, int p_parameter_count, int p_default_args_count, bool p_is_vararg, const Vector<int> &p_extra_allowed_argument_counts, int p_extra_allowed_argument_offset = 0) {
	const int min_argument_count = p_parameter_count - p_default_args_count;
	if (p_argument_count >= min_argument_count && (p_is_vararg || p_argument_count <= p_parameter_count)) {
		return true;
	}

	// `Callable.rpc_id()` prepends a synthetic `peer_id` parameter to the surviving target arity, so
	// each recorded extra arity must be shifted by the offset before comparing against the call's
	// argument count. Other invocations pass a zero offset and are unaffected.
	for (int extra_argument_count : p_extra_allowed_argument_counts) {
		if (extra_argument_count + p_extra_allowed_argument_offset == p_argument_count) {
			return true;
		}
	}

	return false;
}

static bool _rest_parameter_type_is_narrowing(const BSParser::DataType &p_rest_parameter_type) {
	return p_rest_parameter_type.kind == BSParser::DataType::BUILTIN &&
			p_rest_parameter_type.builtin_type == Variant::ARRAY &&
			p_rest_parameter_type.has_container_element_type(0) &&
			!p_rest_parameter_type.get_container_element_type(0).is_variant();
}

BSAnalyzer::CallSiteValidationContext::CallSiteValidationContext(BSAnalyzer *p_analyzer) :
		analyzer(p_analyzer) {
}

void BSAnalyzer::CallSiteValidationContext::validate_call_arg(const MethodInfo &p_method, const BSParser::CallNode *p_call) {
	List<BSParser::DataType> arg_types;

	for (const PropertyInfo &E : p_method.arguments) {
		arg_types.push_back(analyzer->type_from_property(E, true));
	}

	// Cache the resolved parameter types for editor refactors (e.g. insert-explicit-cast),
	// matching the user-function call path. The analyzer owns the parsed tree, so writing
	// through the const handle is sound.
	BSParser::CallNode *mutable_call = const_cast<BSParser::CallNode *>(p_call);
	mutable_call->resolved_parameter_types.clear();
	for (const BSParser::DataType &arg_type : arg_types) {
		mutable_call->resolved_parameter_types.push_back(arg_type);
	}

	validate_call_arg(arg_types, p_method.default_arguments.size(), (p_method.flags & METHOD_FLAG_VARARG) != 0, p_call);
}

String BSAnalyzer::CallSiteValidationContext::make_invalid_argument_error(
		const StringName &p_function,
		int p_argument_number,
		const BSParser::DataType &p_expected_type,
		const BSParser::DataType &p_actual_type,
		bool p_strict_dynamic_mismatch,
		bool p_strict_nullable_mismatch,
		const BSParser::Node *p_actual_node) const {
	(void)p_actual_node;
	if (p_strict_dynamic_mismatch) {
		return vformat(R"*(Cannot pass Variant value as argument %d of "%s()" in strict dynamic mode; expected "%s".)*",
				p_argument_number,
				p_function,
				p_expected_type.to_string_diagnostic());
	}
	if (p_strict_nullable_mismatch) {
		return vformat(R"*(Cannot pass nullable value of type "%s" as argument %d of "%s()"; expected non-nullable "%s".)*",
				p_actual_type.to_string_diagnostic(),
				p_argument_number,
				p_function,
				p_expected_type.to_string_diagnostic());
	}
	return vformat(R"*(Invalid argument for "%s()" function: argument %d should be "%s" but is "%s".)*",
				   p_function,
				   p_argument_number,
				   p_expected_type.to_string_diagnostic(),
				   p_actual_type.to_string_diagnostic()) +
			BSParser::DataType::same_rendered_name_clause(p_expected_type, "parameter", p_actual_type, "argument");
}

const BSParser::DataType *BSAnalyzer::CallSiteValidationContext::rest_element_type(const BSParser::DataType *p_rest_parameter_type) {
	if (p_rest_parameter_type == nullptr || !_rest_parameter_type_is_narrowing(*p_rest_parameter_type)) {
		return nullptr;
	}
	return &p_rest_parameter_type->container_element_types[0];
}

void BSAnalyzer::CallSiteValidationContext::validate_argument_against_type(const BSParser::DataType &p_expected_type, BSParser::ExpressionNode *p_argument, int p_argument_number, const StringName &p_function, const BSParser::CallNode *p_call) {
	(void)p_call;
	if (p_argument == nullptr) {
		return;
	}
	const BSParser::DataType par_type = p_expected_type;
	const BSParser::DataType arg_type = p_argument->get_datatype();

	if (!par_type.is_set() || par_type.is_variant()) {
		return;
	}
	if (!arg_type.is_set() || arg_type.has_no_type()) {
		return;
	}

	if (arg_type.is_variant() || !arg_type.is_hard_type()) {
		if (arg_type.is_variant() && analyzer->strict_dynamic_checks && !(par_type.is_hard_type() && par_type.is_variant())) {
			analyzer->push_error(make_invalid_argument_error(p_function, p_argument_number, par_type, arg_type, true, false, p_argument), p_argument);
		}
		return;
	}

	BSTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = analyzer->strict_dynamic_checks;
	options.strict_null = analyzer->strict_null_checks;
	if (p_argument->is_constant) {
		options.constant_source_value = &p_argument->reduced_value;
	}

	const bool nullable_mismatch = analyzer->strict_null_checks && arg_type.is_nullable && !par_type.is_nullable && !par_type.is_variant();
	if (nullable_mismatch || !BSTypeCompatibility::check(par_type, arg_type, options).compatible) {
		analyzer->push_error(make_invalid_argument_error(p_function, p_argument_number, par_type, arg_type, false, nullable_mismatch, p_argument), p_argument);
	}
}

void BSAnalyzer::CallSiteValidationContext::validate_call_arg(const List<BSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, const BSParser::CallNode *p_call, const Vector<int> &p_extra_allowed_argument_counts, int p_trailing_unbound_argument_count, const BSParser::DataType *p_rest_parameter_type, int p_extra_allowed_argument_offset) {
	if (p_call == nullptr) {
		return;
	}
	if (p_call->arguments.size() < p_par_types.size() - p_default_args_count && !_method_signature_accepts_argument_count(p_call->arguments.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts, p_extra_allowed_argument_offset)) {
		analyzer->push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_call->function_name, p_par_types.size() - p_default_args_count, p_call->arguments.size()), p_call);
	}
	if (!p_is_vararg && p_call->arguments.size() > p_par_types.size() && !_method_signature_accepts_argument_count(p_call->arguments.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts, p_extra_allowed_argument_offset)) {
		analyzer->push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_call->function_name, p_par_types.size(), p_call->arguments.size()), p_call->arguments[p_par_types.size()]);
	}

	const BSParser::DataType *element_type = rest_element_type(p_rest_parameter_type);
	// `unbind(n)` appends n Variant slots for the arguments it drops. Those slots sit after the real
	// fixed parameters, so for a callable with a typed rest tail they must not shadow the rest element
	// for an argument that actually reaches the rest array.
	const int fixed_parameter_count = element_type != nullptr ? MAX(p_par_types.size() - p_trailing_unbound_argument_count, 0) : p_par_types.size();
	List<BSParser::DataType>::ConstIterator par_itr = p_par_types.begin();
	const int checked_argument_count = MAX(p_call->arguments.size() - p_trailing_unbound_argument_count, 0);
	for (int i = 0; i < checked_argument_count; ++i) {
		const BSParser::DataType *expected_type = nullptr;
		if (i < fixed_parameter_count) {
			// A default the analyzer synthesized for a skipped middle parameter is not validated against
			// the (possibly type-parameter-substituted) parameter type, mirroring a trailing omitted
			// default the callee fills in itself.
			if (!p_call->synthesized_argument_indices.has(i)) {
				expected_type = &*par_itr;
			}
			++par_itr;
		} else {
			// Surplus arguments occupy repeated rest-element slots, so they are checked against the
			// rest array's element type under the same policy as a fixed parameter.
			expected_type = element_type;
		}
		if (expected_type == nullptr) {
			continue;
		}
		validate_argument_against_type(*expected_type, p_call->arguments[i], i + 1, p_call->function_name, p_call);
	}
}

BSParser::DataType BSAnalyzer::CallSiteValidationContext::explicit_signal_type_from_info(const MethodInfo &p_info) const {
	BSParser::DataType signal_type = make_signal_type(p_info);
	signal_type.method_parameter_types.clear();
	for (const PropertyInfo &argument : p_info.arguments) {
		signal_type.method_parameter_types.push_back(analyzer->type_from_property(argument, true));
	}
	signal_type.has_explicit_method_signature = true;
	return signal_type;
}

BSParser::DataType BSAnalyzer::CallSiteValidationContext::explicit_signal_type_from_node(const BSParser::SignalNode *p_signal, const BSParser::DataType &p_receiver_type, const BSParser::ClassNode *p_declaring_class) const {
	(void)p_receiver_type;
	(void)p_declaring_class;
	BSParser::DataType signal_type = p_signal->get_datatype();
	signal_type.method_parameter_types.clear();

	// A signal's parameters are written in its declaring class's scope; inherited generic projection
	// remains follow-up under #60 surface/conformance depth.
	for (BSParser::ParameterNode *parameter : p_signal->parameters) {
		if (parameter != nullptr) {
			signal_type.method_parameter_types.push_back(parameter->get_datatype());
		}
	}
	signal_type.has_method_signature = true;
	signal_type.has_explicit_method_signature = true;
	return signal_type;
}

bool BSAnalyzer::CallSiteValidationContext::signal_name_from_constant_arg(const BSParser::CallNode *p_call, int p_signal_arg_index, StringName &r_signal_name) const {
	if (p_signal_arg_index < 0 || p_signal_arg_index >= p_call->arguments.size()) {
		return false;
	}

	const BSParser::ExpressionNode *signal_arg = p_call->arguments[p_signal_arg_index];
	if (signal_arg == nullptr || !signal_arg->is_constant) {
		return false;
	}

	const Variant signal_name_value = signal_arg->reduced_value;
	if (signal_name_value.get_type() != Variant::STRING && signal_name_value.get_type() != Variant::STRING_NAME) {
		return false;
	}

	r_signal_name = signal_name_value;
	return true;
}

bool BSAnalyzer::CallSiteValidationContext::signal_type_from_class_constant_arg(const BSParser::DataType &p_receiver_type, const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const {
	BSParser::ClassNode *class_node = p_receiver_type.class_type;
	if (class_node == nullptr) {
		return false;
	}

	StringName signal_name;
	if (!signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		return false;
	}

	while (class_node != nullptr) {
		if (class_node->has_member(signal_name)) {
			const BSParser::ClassNode::Member &member = class_node->get_member(signal_name);
			if (member.type != BSParser::ClassNode::Member::SIGNAL || member.signal == nullptr) {
				return false;
			}
			r_signal_type = explicit_signal_type_from_node(member.signal, p_receiver_type, class_node);
			return true;
		}

		if (class_node->base_type.kind == BSParser::DataType::NATIVE) {
			return signal_type_from_native_constant_arg(class_node->base_type.native_type, p_call, p_signal_arg_index, r_signal_type);
		}
		class_node = class_node->base_type.class_type;
	}
	return false;
}

bool BSAnalyzer::CallSiteValidationContext::signal_type_from_native_constant_arg(const StringName &p_native_type, const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const {
	if (p_native_type == StringName()) {
		return false;
	}

	StringName signal_name;
	if (!signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		return false;
	}

	MethodInfo signal_info;
	if (!BSNativeDB::get_signal(p_native_type, signal_name, &signal_info)) {
		return false;
	}

	r_signal_type = explicit_signal_type_from_info(signal_info);
	return true;
}

bool BSAnalyzer::CallSiteValidationContext::local_signal_type_from_constant_arg(const BSParser::CallNode *p_call, int p_signal_arg_index, BSParser::DataType &r_signal_type) const {
	if (analyzer->parser == nullptr || analyzer->parser->current_class == nullptr) {
		return false;
	}
	BSParser::DataType receiver = analyzer->parser->current_class->get_datatype();
	receiver.is_meta_type = false;
	return signal_type_from_class_constant_arg(receiver, p_call, p_signal_arg_index, r_signal_type);
}

bool BSAnalyzer::CallSiteValidationContext::call_argument_can_be_string_name(const BSParser::CallNode *p_call, int p_argument_index) {
	if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return false;
	}
	const BSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr) {
		return false;
	}
	const BSParser::DataType type = argument->get_datatype();
	if (type.kind == BSParser::DataType::BUILTIN &&
			(type.builtin_type == Variant::STRING || type.builtin_type == Variant::STRING_NAME)) {
		return true;
	}
	return type.is_variant() || !type.is_set();
}

void BSAnalyzer::CallSiteValidationContext::validate_strict_signal_name_fallback(const BSParser::CallNode *p_call, const BSParser::DataType &p_receiver_type, int p_signal_arg_index) {
	if (!analyzer->strict_dynamic_checks || p_call == nullptr || p_signal_arg_index < 0 || p_signal_arg_index >= p_call->arguments.size()) {
		return;
	}

	StringName signal_name;
	if (signal_name_from_constant_arg(p_call, p_signal_arg_index, signal_name)) {
		analyzer->push_error(vformat(R"*(Cannot resolve signal "%s" on type "%s" for "%s()" in strict dynamic mode.)*",
									 signal_name,
									 p_receiver_type.to_string(),
									 p_call->function_name),
				p_call->arguments[p_signal_arg_index]);
	} else if (call_argument_can_be_string_name(p_call, p_signal_arg_index)) {
		analyzer->push_error(vformat(R"*(Cannot use dynamic signal name for "%s()" on type "%s" in strict dynamic mode.)*",
									 p_call->function_name,
									 p_receiver_type.to_string()),
				p_call->arguments[p_signal_arg_index]);
	}
}

void BSAnalyzer::CallSiteValidationContext::validate_signal_emit_args(const BSParser::DataType &p_signal_type, const BSParser::CallNode *p_call, int p_first_emit_arg_index) {
	if (p_call == nullptr || p_signal_type.kind != BSParser::DataType::BUILTIN || p_signal_type.builtin_type != Variant::SIGNAL || !p_signal_type.has_method_signature) {
		return;
	}

	const int signal_argument_count = p_signal_type.method_parameter_types.size();
	const int emit_argument_count = p_call->arguments.size() - p_first_emit_arg_index;
	if (emit_argument_count < signal_argument_count) {
		analyzer->push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_call->function_name, signal_argument_count + p_first_emit_arg_index, p_call->arguments.size()), p_call);
		return;
	}
	if (emit_argument_count > signal_argument_count) {
		analyzer->push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_call->function_name, signal_argument_count + p_first_emit_arg_index, p_call->arguments.size()), p_call->arguments[signal_argument_count + p_first_emit_arg_index]);
		return;
	}

	// Refine the per-payload parameter types cached for downstream call handling.
	// The generic `Object.emit_signal` vararg signature recorded Variant payload slots; overwrite
	// them with the resolved signal parameter types, aligned to the call's actual argument indices
	// (the leading name argument occupies the slots before `p_first_emit_arg_index`). The analyzer
	// owns the parsed tree, so writing through the const handle is sound.
	BSParser::CallNode *mutable_call = const_cast<BSParser::CallNode *>(p_call);
	if (mutable_call->resolved_parameter_types.size() < p_first_emit_arg_index + signal_argument_count) {
		mutable_call->resolved_parameter_types.resize(p_first_emit_arg_index + signal_argument_count);
	}
	for (int i = 0; i < signal_argument_count; i++) {
		mutable_call->resolved_parameter_types.write[p_first_emit_arg_index + i] = p_signal_type.method_parameter_types[i];
	}

	for (int i = 0; i < signal_argument_count; i++) {
		const int emit_argument_index = p_first_emit_arg_index + i;
		const BSParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
		validate_argument_against_type(signal_parameter_type, p_call->arguments[emit_argument_index], emit_argument_index + 1, p_call->function_name, p_call);
	}
}

void BSAnalyzer::CallSiteValidationContext::validate_local_object_emit_signal_args(const BSParser::CallNode *p_call, bool p_is_self) {
	if (!p_is_self || p_call == nullptr || p_call->function_name != SNAME("emit_signal") || p_call->arguments.is_empty()) {
		return;
	}

	BSParser::DataType signal_type;
	if (!local_signal_type_from_constant_arg(p_call, 0, signal_type)) {
		if (analyzer->parser != nullptr && analyzer->parser->current_class != nullptr) {
			validate_strict_signal_name_fallback(p_call, analyzer->parser->current_class->get_datatype(), 0);
		}
		return;
	}

	validate_signal_emit_args(signal_type, p_call, 1);
}

} // namespace barista_script
