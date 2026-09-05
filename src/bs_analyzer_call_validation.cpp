/**************************************************************************/
/*  bs_analyzer_call_validation.cpp                                       */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_analyzer_call_        */
/*  validation.cpp` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6. FS* ->    */
/*  BS*; engine contact through bs_platform.h. MethodInfo / typed call    */
/*  arity+types, signal emit / emit_signal, named-arg canonicalization,   */
/*  signal connect/callable signature validation, and Callable            */
/*  bind/bindv/unbind/call/callv/call_deferred/rpc/rpc_id transforms      */
/*  for #60.                                                              */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "bs_native_db.h"

namespace barista_script {

// Foundry make_coroutine_type @ c9d5e35 (~1646): wrap result T as Coroutine[T]. Principal identity
// is the native BSFunctionState skin; is_coroutine discriminates await / missing-await; the phantom
// result type lives in container_element_types[0].
static BSParser::DataType make_coroutine_type(const BSParser::DataType &p_result_type) {
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
			// A vararg slot past the declared parameters, and a default the analyzer synthesized, carry
			// no parameter type. A contextual case shorthand written there has no union to take.
			analyzer->resolve_contextual_enum_case(p_call->arguments[i], BSParser::DataType());
			continue;
		}
		// A contextual case shorthand in argument position takes its union from the parameter type.
		analyzer->qualify_contextual_enum_case_consumer(p_call->arguments[i], *expected_type);
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

static BSParser::DataType make_void_type() {
	BSParser::DataType void_type;
	void_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	void_type.kind = BSParser::DataType::BUILTIN;
	void_type.builtin_type = Variant::NIL;
	return void_type;
}

static String callable_type_string_with_signature(
		const BSParser::DataType &p_callable_type,
		const Vector<BSParser::DataType> &p_callable_parameter_types) {
	if (p_callable_type.has_explicit_method_signature || p_callable_parameter_types.is_empty()) {
		return p_callable_type.to_string();
	}

	BSParser::DataType callable_type = p_callable_type;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_callable_parameter_types;
	callable_type.method_return_type.push_back(make_void_type());
	return callable_type.to_string();
}

// A locally declared signal carries its per-parameter signature without the explicit-annotation flag,
// so `to_string()` would render it as a bare "Signal". Rendering it from the parameters it does carry
// keeps the diagnostics for `mysignal.connect(handler)` identical to `connect("mysignal", handler)`.
static String signal_type_string_with_signature(const BSParser::DataType &p_signal_type) {
	if (p_signal_type.has_explicit_method_signature || p_signal_type.method_parameter_types.is_empty()) {
		return p_signal_type.to_string();
	}

	BSParser::DataType signal_type = p_signal_type;
	signal_type.has_explicit_method_signature = true;
	return signal_type.to_string();
}

bool BSAnalyzer::CallSiteValidationContext::call_has_named_arguments(const BSParser::CallNode *p_call) {
	if (p_call == nullptr) {
		return false;
	}
	for (int i = 0; i < p_call->argument_names.size(); i++) {
		if (p_call->argument_names[i] != StringName()) {
			return true;
		}
	}
	return false;
}

void BSAnalyzer::CallSiteValidationContext::reject_named_call_arguments(const BSParser::CallNode *p_call) {
	// Named arguments are resolved entirely at compile time against a statically known
	// BaristaScript signature. For any other callee (builtin constructors, engine utility
	// functions, native methods, or dynamic/`Callable` targets) the parameter names are
	// unavailable, so reject them instead of silently dropping the names.
	if (p_call == nullptr) {
		return;
	}
	for (int i = 0; i < p_call->argument_names.size(); i++) {
		if (p_call->argument_names[i] != StringName()) {
			analyzer->push_error("Named arguments require a statically known BaristaScript function.", p_call->arguments[i]);
			return;
		}
	}
}

bool BSAnalyzer::CallSiteValidationContext::canonicalize_named_call_arguments(BSParser::CallNode *p_call, const BSParser::FunctionNode *p_function) {
	// The parser keeps `argument_names` parallel to `arguments`, with an empty name for each
	// positional argument. Map every `name = value` argument to its parameter position, rewrite
	// the call into canonical positional order, and clear the names so the rest of the analyzer,
	// codegen, and the VM see an ordinary positional call.
	if (p_call == nullptr || p_function == nullptr) {
		return true;
	}
	if (p_call->argument_names.size() != p_call->arguments.size() || !call_has_named_arguments(p_call)) {
		// Nothing to canonicalize; drop any (all-empty) name metadata for a clean positional call.
		p_call->argument_names.clear();
		return true;
	}

	const int parameter_count = p_function->parameters.size();
	const StringName function_name = p_function->identifier != nullptr ? p_function->identifier->name : StringName();

	// Rule: once a named argument appears, every following argument must be named.
	int positional_count = 0;
	bool seen_named = false;
	for (int i = 0; i < p_call->arguments.size(); i++) {
		const bool is_named = p_call->argument_names[i] != StringName();
		if (is_named) {
			seen_named = true;
		} else if (seen_named) {
			analyzer->push_error("Positional argument cannot follow a named argument.", p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		} else {
			positional_count++;
		}
	}

	Vector<BSParser::ExpressionNode *> slots;
	slots.resize(parameter_count);
	for (int i = 0; i < parameter_count; i++) {
		slots.write[i] = nullptr;
	}

	// Track the source (written) position of the argument occupying each slot so the canonical call
	// can carry a source-order evaluation list for codegen. A slot left at -1 holds either nothing or
	// a synthesized constant gap-fill, neither of which has an observable evaluation position.
	Vector<int> slot_source_index;
	slot_source_index.resize(parameter_count);
	for (int i = 0; i < parameter_count; i++) {
		slot_source_index.write[i] = -1;
	}

	// Positional arguments fill the leading parameter slots in order. Arguments beyond the fixed
	// parameter count belong to a rest parameter and keep their order after the fixed slots.
	Vector<BSParser::ExpressionNode *> rest_arguments;
	Vector<int> rest_source_index;
	for (int i = 0; i < positional_count; i++) {
		if (i < parameter_count) {
			slots.write[i] = p_call->arguments[i];
			slot_source_index.write[i] = i;
		} else {
			rest_arguments.push_back(p_call->arguments[i]);
			rest_source_index.push_back(i);
		}
	}

	for (int i = positional_count; i < p_call->arguments.size(); i++) {
		const StringName &argument_name = p_call->argument_names[i];
		if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr && p_function->rest_parameter->identifier->name == argument_name) {
			analyzer->push_error(vformat(R"(The rest parameter "%s" cannot be passed by name.)", argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		const int *parameter_index = p_function->parameters_indices.getptr(argument_name);
		if (parameter_index == nullptr) {
			analyzer->push_error(vformat(R"*(Function "%s()" has no parameter named "%s".)*", function_name, argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		if (slots[*parameter_index] != nullptr) {
			analyzer->push_error(vformat(R"(Parameter "%s" was specified more than once.)", argument_name), p_call->arguments[i]);
			p_call->argument_names.clear();
			return false;
		}
		slots.write[*parameter_index] = p_call->arguments[i];
		slot_source_index.write[*parameter_index] = i;
	}

	int max_filled_index = -1;
	for (int i = 0; i < parameter_count; i++) {
		if (slots[i] != nullptr) {
			max_filled_index = i;
		}
	}

	p_call->synthesized_argument_indices.clear();
	for (int i = 0; i < parameter_count; i++) {
		if (slots[i] != nullptr) {
			continue;
		}

		const BSParser::ParameterNode *parameter = p_function->parameters[i];
		const StringName parameter_name = parameter != nullptr && parameter->identifier != nullptr ? parameter->identifier->name : StringName();

		if (parameter == nullptr || parameter->initializer == nullptr) {
			analyzer->push_error(vformat(R"(Missing value for required parameter "%s".)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}

		if (i >= max_filled_index) {
			// Trailing optional parameter: leave it out so the callee supplies its own default.
			continue;
		}

		if (!parameter->initializer->is_constant) {
			analyzer->push_error(vformat(R"(Cannot skip parameter "%s": its default value is not a constant expression. Pass it explicitly.)", parameter_name), p_call);
			p_call->argument_names.clear();
			return false;
		}

		if (analyzer->parser == nullptr) {
			p_call->argument_names.clear();
			return false;
		}
		BSParser::LiteralNode *constant_argument = analyzer->parser->alloc_node<BSParser::LiteralNode>();
		constant_argument->value = parameter->initializer->reduced_value;
		constant_argument->reduced = true;
		constant_argument->is_constant = true;
		constant_argument->reduced_value = parameter->initializer->reduced_value;
		constant_argument->set_datatype(parameter->initializer->get_datatype());
		slots.write[i] = constant_argument;
		p_call->synthesized_argument_indices.insert(i);
	}

	Vector<BSParser::ExpressionNode *> canonical_arguments;
	Vector<int> canonical_source_index;
	for (int i = 0; i <= max_filled_index; i++) {
		canonical_arguments.push_back(slots[i]);
		canonical_source_index.push_back(slot_source_index[i]);
	}
	for (int i = 0; i < rest_arguments.size(); i++) {
		canonical_arguments.push_back(rest_arguments[i]);
		canonical_source_index.push_back(rest_source_index[i]);
	}

	Vector<int> evaluation_order;
	bool reordered = false;
	for (int source = 0; source < p_call->arguments.size(); source++) {
		for (int canonical = 0; canonical < canonical_source_index.size(); canonical++) {
			if (canonical_source_index[canonical] == source) {
				if (canonical != evaluation_order.size()) {
					reordered = true;
				}
				evaluation_order.push_back(canonical);
				break;
			}
		}
	}
	for (int canonical = 0; canonical < canonical_source_index.size(); canonical++) {
		if (canonical_source_index[canonical] == -1) {
			if (canonical != evaluation_order.size()) {
				reordered = true;
			}
			evaluation_order.push_back(canonical);
		}
	}

	p_call->arguments = canonical_arguments;
	p_call->argument_names.clear();
	if (reordered) {
		p_call->argument_evaluation_order = evaluation_order;
	}
	return true;
}

bool BSAnalyzer::CallSiteValidationContext::callable_signature_from_type(const BSParser::DataType &p_callable_type, Vector<BSParser::DataType> &r_par_types, int &r_default_arg_count, bool &r_is_vararg) const {
	if (p_callable_type.kind != BSParser::DataType::BUILTIN || p_callable_type.builtin_type != Variant::CALLABLE || !p_callable_type.has_method_signature) {
		return false;
	}

	r_par_types.clear();
	r_default_arg_count = 0;
	r_is_vararg = false;

	if (p_callable_type.has_explicit_method_signature) {
		r_par_types = p_callable_type.method_parameter_types;
		r_default_arg_count = p_callable_type.method_info.default_arguments.size();
		r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
		return true;
	}

	if (p_callable_type.method_parameter_types.size() == p_callable_type.method_info.arguments.size()) {
		r_par_types = p_callable_type.method_parameter_types;
		r_default_arg_count = p_callable_type.method_info.default_arguments.size();
		r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
		return true;
	}

	for (const PropertyInfo &E : p_callable_type.method_info.arguments) {
		r_par_types.push_back(analyzer->type_from_property(E, true));
	}
	r_default_arg_count = p_callable_type.method_info.default_arguments.size();
	r_is_vararg = (p_callable_type.method_info.flags & METHOD_FLAG_VARARG) != 0;
	return true;
}

BSParser::DataType BSAnalyzer::CallSiteValidationContext::callable_type_from_function(const BSParser::FunctionNode *p_function) const {
	BSParser::DataType type;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = Variant::CALLABLE;
	type.is_constant = true;
	type.has_method_signature = true;
	// Foundry MEMBER_FUNCTION / make_callable_type @ c9d5e35: bare function refs publish an
	// explicit signature so Callable.bind/unbind/call/callv/rpc transforms can validate it.
	type.has_explicit_method_signature = true;
	if (p_function == nullptr) {
		return type;
	}

	type.method_info = p_function->info;
	type.signature_is_async = p_function->is_coroutine;
	for (BSParser::ParameterNode *parameter : p_function->parameters) {
		if (parameter != nullptr) {
			type.method_parameter_types.push_back(parameter->get_datatype());
		}
	}
	if (p_function->is_vararg() && p_function->rest_parameter != nullptr) {
		const BSParser::DataType rest_type = p_function->rest_parameter->get_datatype();
		if (rest_type.kind == BSParser::DataType::BUILTIN && rest_type.builtin_type == Variant::ARRAY &&
				rest_type.has_container_element_type(0) && !rest_type.get_container_element_type(0).is_variant()) {
			type.set_method_rest_parameter_type(rest_type);
		}
	}
	type.method_return_type.push_back(p_function->get_datatype());
	return type;
}

BSParser::DataType BSAnalyzer::CallSiteValidationContext::plain_callable_type() const {
	return analyzer->type_from_property(PropertyInfo(Variant::CALLABLE, ""));
}

BSParser::DataType BSAnalyzer::CallSiteValidationContext::over_bound_callable_type(const BSParser::DataType &p_source_callable_type) const {
	// Binding more arguments than a fixed-arity target accepts produces a callable that cannot be
	// invoked successfully. There is no precise signature to describe it, so return a signatureless
	// callable that still carries the source's async marker (bind()/unbind() preserve async-ness).
	BSParser::DataType callable_type = plain_callable_type();
	callable_type.signature_is_async = p_source_callable_type.signature_is_async;
	callable_type.callable_is_over_bound = true;
	return callable_type;
}

BSParser::DataType BSAnalyzer::CallSiteValidationContext::transformed_callable_type(const BSParser::DataType &p_source_callable_type, const Vector<BSParser::DataType> &p_parameter_types, int p_default_arg_count, bool p_is_vararg) const {
	BSParser::DataType callable_type = p_source_callable_type;
	callable_type.kind = BSParser::DataType::BUILTIN;
	callable_type.builtin_type = Variant::CALLABLE;
	callable_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	callable_type.has_method_signature = true;
	callable_type.has_explicit_method_signature = true;
	callable_type.method_parameter_types = p_parameter_types;
	callable_type.method_extra_allowed_argument_counts.clear();
	callable_type.method_unbound_argument_count = 0;
	callable_type.callable_is_over_bound = false;
	callable_type.method_info.arguments.clear();
	for (int i = 0; i < p_parameter_types.size(); i++) {
		callable_type.method_info.arguments.push_back(p_parameter_types[i].to_property_info("arg" + itos(i + 1)));
	}
	callable_type.method_info.default_arguments.clear();
	callable_type.method_info.default_arguments.resize(p_default_arg_count);
	if (p_is_vararg) {
		callable_type.method_info.flags |= METHOD_FLAG_VARARG;
	} else {
		callable_type.method_info.flags &= ~uint32_t(METHOD_FLAG_VARARG);
	}
	if (!callable_type.method_return_type.is_empty()) {
		callable_type.method_info.return_val = callable_type.method_return_type[0].to_property_info("");
	}
	return callable_type;
}

BSParser::ArrayNode *BSAnalyzer::CallSiteValidationContext::array_literal_argument(const BSParser::CallNode *p_call, int p_argument_index) const {
	if (p_call == nullptr || p_argument_index < 0 || p_argument_index >= p_call->arguments.size()) {
		return nullptr;
	}
	BSParser::ExpressionNode *argument = p_call->arguments[p_argument_index];
	if (argument == nullptr || argument->type != BSParser::Node::ARRAY) {
		return nullptr;
	}
	return static_cast<BSParser::ArrayNode *>(argument);
}

void BSAnalyzer::CallSiteValidationContext::validate_callable_array_literal_args(const Vector<BSParser::DataType> &p_par_types, int p_default_args_count, bool p_is_vararg, BSParser::ArrayNode *p_array, const StringName &p_function, const Vector<int> &p_extra_allowed_argument_counts, int p_trailing_unbound_argument_count, const BSParser::DataType *p_rest_parameter_type) {
	// Foundry CallSiteValidationContext::validate_callable_array_literal_args @ c9d5e35 —
	// Callable.callv([...]) checks a single array literal against the surviving target arity/types.
	if (p_array == nullptr) {
		return;
	}

	if (p_array->elements.size() < p_par_types.size() - p_default_args_count && !_method_signature_accepts_argument_count(p_array->elements.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Too few arguments for "%s()" call. Expected at least %d but received %d.)*", p_function, p_par_types.size() - p_default_args_count, p_array->elements.size()), p_array);
	}
	if (!p_is_vararg && p_array->elements.size() > p_par_types.size() && !_method_signature_accepts_argument_count(p_array->elements.size(), p_par_types.size(), p_default_args_count, p_is_vararg, p_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Too many arguments for "%s()" call. Expected at most %d but received %d.)*", p_function, p_par_types.size(), p_array->elements.size()), p_array->elements[p_par_types.size()]);
	}

	const BSParser::DataType *element_type = rest_element_type(p_rest_parameter_type);
	// See validate_call_arg: the trailing Variant slots `unbind(n)` appends must not shadow a typed
	// rest element for an element that actually reaches the rest array.
	const int fixed_parameter_count = element_type != nullptr ? MAX(p_par_types.size() - p_trailing_unbound_argument_count, 0) : p_par_types.size();
	const int checked_argument_count = MAX(p_array->elements.size() - p_trailing_unbound_argument_count, 0);
	for (int i = 0; i < checked_argument_count; i++) {
		// Surplus literal elements occupy repeated rest-element slots, so they are checked against the
		// rest array's element type under the same policy as a fixed parameter.
		const BSParser::DataType *expected_type = i < fixed_parameter_count ? &p_par_types[i] : element_type;
		if (expected_type == nullptr) {
			break;
		}
		validate_argument_against_type(*expected_type, p_array->elements[i], i + 1, p_function, nullptr);
	}
}

void BSAnalyzer::CallSiteValidationContext::validate_signal_connect_arg(const BSParser::DataType &p_signal_type, const BSParser::CallNode *p_call, int p_callable_arg_index) {
	if (p_call == nullptr || (p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) || p_callable_arg_index < 0 || p_callable_arg_index >= p_call->arguments.size()) {
		return;
	}
	if (p_signal_type.kind != BSParser::DataType::BUILTIN || p_signal_type.builtin_type != Variant::SIGNAL || !p_signal_type.has_method_signature) {
		return;
	}
	if (p_signal_type.method_parameter_types.size() != p_signal_type.method_info.arguments.size()) {
		// The rich per-parameter signature was dropped in favor of the MethodInfo form because its slots
		// could not be compared reliably across the script-API boundary. Its (empty) parameter list would
		// make every handler look like an arity mismatch, so leave such a signal unvalidated.
		return;
	}

	const BSParser::DataType callable_type = p_call->arguments[p_callable_arg_index]->get_datatype();
	Vector<BSParser::DataType> callable_parameter_types;
	int callable_default_arg_count = 0;
	bool callable_is_vararg = false;
	if (!callable_signature_from_type(callable_type, callable_parameter_types, callable_default_arg_count, callable_is_vararg)) {
		return;
	}

	const int signal_argument_count = p_signal_type.method_parameter_types.size();
	const int callable_argument_count = callable_parameter_types.size();
	const int callable_min_argument_count = callable_argument_count - callable_default_arg_count;
	const StringName action_name = p_call->function_name == SNAME("disconnect") ? SNAME("disconnect") : (p_call->function_name == SNAME("is_connected") ? StringName("check connection for") : SNAME("connect"));
	const String callable_type_string = callable_type_string_with_signature(callable_type, callable_parameter_types);
	const String signal_type_string = signal_type_string_with_signature(p_signal_type);
	if (!_method_signature_accepts_argument_count(signal_argument_count, callable_argument_count, callable_default_arg_count, callable_is_vararg, callable_type.method_extra_allowed_argument_counts)) {
		analyzer->push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal emits %d arguments but callable expects %s%d.)*",
									 action_name,
									 signal_type_string,
									 callable_type_string,
									 signal_argument_count,
									 callable_default_arg_count > 0 ? "at least " : "",
									 callable_default_arg_count > 0 ? callable_min_argument_count : callable_argument_count),
				p_call->arguments[p_callable_arg_index]);
		return;
	}

	BSTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;
	options.strict_dynamic = true;
	options.strict_null = analyzer->strict_null_checks;

	const int checked_signal_argument_count = MAX(signal_argument_count - callable_type.method_unbound_argument_count, 0);
	for (int i = 0; i < checked_signal_argument_count && i < callable_argument_count; i++) {
		const BSParser::DataType &callable_parameter_type = callable_parameter_types[i];
		const BSParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
		const bool nullable_mismatch = analyzer->strict_null_checks && signal_parameter_type.is_nullable && !callable_parameter_type.is_nullable && !callable_parameter_type.is_variant();
		if (nullable_mismatch || !BSTypeCompatibility::check(callable_parameter_type, signal_parameter_type, options).compatible) {
			if (nullable_mismatch) {
				analyzer->push_error(vformat("Cannot %s signal \"%s\" to callable \"%s\": signal argument %d is nullable "
											 "type \"%s\", but callable parameter expects non-nullable \"%s\".",
											 action_name,
											 signal_type_string,
											 callable_type_string,
											 i + 1,
											 signal_parameter_type.to_string(),
											 callable_parameter_type.to_string()),
						p_call->arguments[p_callable_arg_index]);
			} else {
				analyzer->push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal argument %d of type "%s" cannot be passed to callable parameter of type "%s".)*",
											 action_name,
											 signal_type_string,
											 callable_type_string,
											 i + 1,
											 signal_parameter_type.to_string(),
											 callable_parameter_type.to_string()),
						p_call->arguments[p_callable_arg_index]);
			}
			return;
		}
	}

	// A signal never becomes variadic, but a variadic handler receives every signal argument past its
	// fixed prefix through its rest tail, so each of those must fit the tail's element type.
	if (callable_is_vararg && callable_type.has_method_rest_parameter_type()) {
		const BSParser::DataType &rest_array = callable_type.get_method_rest_parameter_type();
		if (rest_array.has_container_element_type(0)) {
			const BSParser::DataType rest_element = rest_array.get_container_element_type(0);
			const int callable_fixed_argument_count =
					MAX(callable_argument_count - callable_type.method_unbound_argument_count, 0);
			for (int i = callable_fixed_argument_count; i < checked_signal_argument_count; i++) {
				const BSParser::DataType &signal_parameter_type = p_signal_type.method_parameter_types[i];
				if (!BSTypeCompatibility::check(rest_element, signal_parameter_type, options).compatible) {
					analyzer->push_error(vformat(R"*(Cannot %s signal "%s" to callable "%s": signal argument %d of type "%s" cannot be passed to callable rest parameter of type "%s".)*",
												 action_name,
												 signal_type_string,
												 callable_type_string,
												 i + 1,
												 signal_parameter_type.to_string(),
												 rest_array.to_string()),
							p_call->arguments[p_callable_arg_index]);
					return;
				}
			}
		}
	}
}

void BSAnalyzer::CallSiteValidationContext::validate_local_object_signal_callable_arg(const BSParser::CallNode *p_call, bool p_is_self) {
	if (!p_is_self || p_call == nullptr || (p_call->function_name != SNAME("connect") && p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) || p_call->arguments.size() < 2) {
		return;
	}

	BSParser::DataType signal_type;
	if (!local_signal_type_from_constant_arg(p_call, 0, signal_type)) {
		if (analyzer->parser != nullptr && analyzer->parser->current_class != nullptr) {
			validate_strict_signal_name_fallback(p_call, analyzer->parser->current_class->get_datatype(), 0);
		}
		return;
	}

	validate_signal_connect_arg(signal_type, p_call, 1);
}

bool BSAnalyzer::CallSiteValidationContext::try_type_callable_method_call(BSParser::CallNode *p_call, const BSParser::DataType &p_base_type) {
	// Foundry get_function_signature Callable.bind/bindv/unbind/call/callv/call_deferred/rpc/rpc_id
	// slice (@ c9d5e35), including AsyncCallable→coroutine wrapping on synchronous call/callv.
	// Await unwrap + MISSING_AWAIT live in reduce_await / reduce_call (#60).
	if (p_call == nullptr || p_base_type.kind != BSParser::DataType::BUILTIN || p_base_type.builtin_type != Variant::CALLABLE) {
		return false;
	}

	const StringName function_name = p_call->function_name;
	const bool is_callable_call = function_name == SNAME("call");
	const bool is_callable_callv = function_name == SNAME("callv");
	const bool is_callable_call_deferred = function_name == SNAME("call_deferred");
	const bool is_callable_rpc = function_name == SNAME("rpc");
	const bool is_callable_rpc_id = function_name == SNAME("rpc_id");
	const bool is_callable_bind = function_name == SNAME("bind");
	const bool is_callable_bindv = function_name == SNAME("bindv");
	const bool is_callable_unbind = function_name == SNAME("unbind");
	const bool is_callable_invocation = is_callable_call || is_callable_callv || is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id;
	if (!is_callable_invocation && !is_callable_bind && !is_callable_bindv && !is_callable_unbind) {
		return false;
	}

	reject_named_call_arguments(p_call);

	if (p_base_type.callable_is_over_bound) {
		if (is_callable_invocation) {
			analyzer->push_error(R"(Cannot invoke this Callable: it was over-bound (more arguments were bound than its target accepts), so the call can never succeed.)", p_call);
			BSParser::DataType void_or_variant = analyzer->type_from_property(PropertyInfo(Variant::NIL, ""));
			p_call->set_datatype(void_or_variant);
			return true;
		}
		if (is_callable_bind || is_callable_bindv || is_callable_unbind) {
			// bind()/bindv()/unbind() keep producing an over-bound callable so a later invocation is still flagged.
			p_call->set_datatype(over_bound_callable_type(p_base_type));
			return true;
		}
		return false;
	}

	if (!p_base_type.has_explicit_method_signature) {
		// Foundry bare AsyncCallable @ c9d5e35 (~16658): signatureless `var cb: AsyncCallable` still
		// yields a coroutine from call/callv (untyped Variant result wrapped). Deferred/RPC stay NIL.
		if (p_base_type.signature_is_async && (is_callable_call || is_callable_callv)) {
			BSParser::DataType variant_return;
			variant_return.kind = BSParser::DataType::VARIANT;
			p_call->set_datatype(make_coroutine_type(variant_return));
			return true;
		}
		return false;
	}

	const bool is_callable_vararg = (p_base_type.method_info.flags & METHOD_FLAG_VARARG) != 0;

	auto bound_argument_conflicts_with = [&](const BSParser::ExpressionNode *p_argument, const BSParser::DataType &p_expected_type) -> bool {
		if (p_argument == nullptr || !p_expected_type.is_hard_type() || p_expected_type.is_variant()) {
			return false;
		}
		const BSParser::DataType argument_type = p_argument->get_datatype();
		if (argument_type.is_variant() || !argument_type.is_hard_type()) {
			return analyzer->strict_dynamic_checks && argument_type.is_variant();
		}
		// Foundry @ c9d5e35: conflict only when incompatible and not runtime-narrowable, so
		// native subtype binds can still shift trailing defaults (default_survival_for_bind).
		return !BSTypeCompatibility::is_compatible(p_expected_type, argument_type, true) &&
				!BSTypeCompatibility::allows_runtime_narrowing(p_expected_type, argument_type);
	};

	auto bound_arguments_reaching_target = [&](const Vector<const BSParser::ExpressionNode *> &p_bound_arguments) -> int {
		return MAX(p_bound_arguments.size() - p_base_type.method_unbound_argument_count, 0);
	};

	auto default_survival_for_bind = [&](const Vector<const BSParser::ExpressionNode *> &p_bound_arguments,
											 int p_checked_bind_start, int p_remaining_argument_count,
											 int &r_result_default_arg_count, Vector<int> &r_extra_allowed_argument_counts) {
		r_result_default_arg_count = 0;
		const int max_shift = MIN(int(p_base_type.method_info.default_arguments.size()), p_checked_bind_start);
		const int reaching_bound_argument_count = bound_arguments_reaching_target(p_bound_arguments);
		for (int shift = 1; shift <= max_shift; shift++) {
			const int shifted_start = p_checked_bind_start - shift;
			bool bound_arguments_fit_shift = true;
			for (int i = 0; i < reaching_bound_argument_count && bound_arguments_fit_shift; i++) {
				bound_arguments_fit_shift = !bound_argument_conflicts_with(
						p_bound_arguments[i], p_base_type.method_parameter_types[shifted_start + i]);
			}
			if (!bound_arguments_fit_shift) {
				continue;
			}
			if (shift == r_result_default_arg_count + 1) {
				r_result_default_arg_count = shift;
			} else {
				r_extra_allowed_argument_counts.push_back(p_remaining_argument_count - shift);
			}
		}
	};

	auto preserve_extra_allowed_argument_counts = [&](const Vector<const BSParser::ExpressionNode *> &p_bound_arguments, BSParser::DataType &r_return_type) {
		for (int extra_allowed_argument_count : p_base_type.method_extra_allowed_argument_counts) {
			const int remaining_argument_count = extra_allowed_argument_count - p_bound_arguments.size();
			if (remaining_argument_count < 0 || extra_allowed_argument_count > p_base_type.method_parameter_types.size()) {
				continue;
			}
			bool bound_arguments_fit_extra_arity = true;
			for (int i = 0; i < p_bound_arguments.size() && bound_arguments_fit_extra_arity; i++) {
				bound_arguments_fit_extra_arity = !bound_argument_conflicts_with(
						p_bound_arguments[i], p_base_type.method_parameter_types[remaining_argument_count + i]);
			}
			if (bound_arguments_fit_extra_arity) {
				r_return_type.method_extra_allowed_argument_counts.push_back(remaining_argument_count);
			}
		}
	};

	if (is_callable_call || is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id) {
		List<BSParser::DataType> par_types;
		// `Callable.rpc_id()` prepends a synthetic peer_id before the surviving target parameters.
		constexpr int callable_rpc_id_peer_id_argument_count = 1;
		const int extra_allowed_argument_offset = is_callable_rpc_id ? callable_rpc_id_peer_id_argument_count : 0;
		if (is_callable_rpc_id) {
			par_types.push_back(analyzer->type_from_property(PropertyInfo(Variant::INT, "peer_id"), true));
		}
		for (const BSParser::DataType &parameter_type : p_base_type.method_parameter_types) {
			par_types.push_back(parameter_type);
		}
		const BSParser::DataType *rest_type = p_base_type.has_method_rest_parameter_type() ? &p_base_type.get_method_rest_parameter_type() : nullptr;
		validate_call_arg(par_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg, p_call,
				p_base_type.method_extra_allowed_argument_counts, p_base_type.method_unbound_argument_count, rest_type,
				extra_allowed_argument_offset);

		BSParser::DataType return_type;
		if (is_callable_call_deferred || is_callable_rpc || is_callable_rpc_id || p_base_type.method_return_type.is_empty()) {
			// Deferred/RPC dispatches do not return the callee's value; Foundry types them as NIL.
			return_type = analyzer->type_from_property(PropertyInfo(Variant::NIL, ""));
		} else {
			return_type = p_base_type.method_return_type[0];
		}
		// Synchronously invoking an AsyncCallable yields a coroutine (must be awaited).
		// Deferred/RPC dispatches do not return the callee's value, so they stay non-coroutine.
		if (is_callable_call && p_base_type.signature_is_async) {
			return_type = make_coroutine_type(return_type);
		}
		p_call->set_datatype(return_type);
		return true;
	}

	if (is_callable_callv) {
		List<BSParser::DataType> callv_par_types;
		callv_par_types.push_back(analyzer->type_from_property(PropertyInfo(Variant::ARRAY, "arguments"), true));
		validate_call_arg(callv_par_types, 0, false, p_call);

		BSParser::DataType return_type;
		if (p_base_type.method_return_type.is_empty()) {
			return_type = analyzer->type_from_property(PropertyInfo(Variant::NIL, ""));
		} else {
			return_type = p_base_type.method_return_type[0];
		}
		// As with call(), invoking an AsyncCallable through callv() yields a coroutine.
		if (p_base_type.signature_is_async) {
			return_type = make_coroutine_type(return_type);
		}

		const BSParser::DataType *callv_rest_type = p_base_type.has_method_rest_parameter_type() ? &p_base_type.get_method_rest_parameter_type() : nullptr;
		validate_callable_array_literal_args(p_base_type.method_parameter_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg,
				array_literal_argument(p_call, 0), function_name, p_base_type.method_extra_allowed_argument_counts,
				p_base_type.method_unbound_argument_count, callv_rest_type);

		p_call->set_datatype(return_type);
		return true;
	}

	if (is_callable_bind) {
		List<BSParser::DataType> bind_par_types;
		BSParser::DataType return_type = plain_callable_type();
		return_type.signature_is_async = p_base_type.signature_is_async;

		if (!is_callable_vararg) {
			const int bind_argument_count = p_call->arguments.size();
			const int callable_argument_count = p_base_type.method_parameter_types.size();
			const int checked_bind_argument_count = MIN(bind_argument_count, callable_argument_count);
			const int checked_bind_start = callable_argument_count - checked_bind_argument_count;

			for (int i = 0; i < checked_bind_argument_count; i++) {
				bind_par_types.push_back(p_base_type.method_parameter_types[checked_bind_start + i]);
			}
			// Callable.bind() is variadic: accept any number of bound arguments without an arity error.
			validate_call_arg(bind_par_types, 0, true, p_call);

			if (bind_argument_count > callable_argument_count) {
				return_type = over_bound_callable_type(p_base_type);
			} else {
				Vector<BSParser::DataType> remaining_parameter_types;
				const int remaining_argument_count = callable_argument_count - bind_argument_count;
				for (int i = 0; i < remaining_argument_count; i++) {
					remaining_parameter_types.push_back(p_base_type.method_parameter_types[i]);
				}

				Vector<const BSParser::ExpressionNode *> bound_arguments;
				for (BSParser::ExpressionNode *argument : p_call->arguments) {
					bound_arguments.push_back(argument);
				}

				int remaining_default_arg_count = 0;
				Vector<int> default_survival_extra_argument_counts;
				default_survival_for_bind(bound_arguments, checked_bind_start, remaining_argument_count,
						remaining_default_arg_count, default_survival_extra_argument_counts);

				return_type = transformed_callable_type(p_base_type, remaining_parameter_types, remaining_default_arg_count, false);
				for (int extra_allowed_argument_count : default_survival_extra_argument_counts) {
					return_type.method_extra_allowed_argument_counts.push_back(extra_allowed_argument_count);
				}
				preserve_extra_allowed_argument_counts(bound_arguments, return_type);
			}
		} else {
			// Variadic Callable.bind richness (preserve_fixed_vararg_callable) remains follow-up under #60.
			validate_call_arg(List<BSParser::DataType>(), 0, true, p_call);
			return_type = plain_callable_type();
			return_type.signature_is_async = p_base_type.signature_is_async;
		}

		p_call->set_datatype(return_type);
		return true;
	}

	if (is_callable_bindv) {
		List<BSParser::DataType> bindv_par_types;
		bindv_par_types.push_back(analyzer->type_from_property(PropertyInfo(Variant::ARRAY, "arguments"), true));
		validate_call_arg(bindv_par_types, 0, false, p_call);

		BSParser::DataType return_type = plain_callable_type();
		return_type.signature_is_async = p_base_type.signature_is_async;

		BSParser::ArrayNode *bind_array = array_literal_argument(p_call, 0);
		if (!is_callable_vararg && bind_array != nullptr) {
			const int bind_argument_count = bind_array->elements.size();
			const int callable_argument_count = p_base_type.method_parameter_types.size();
			const int checked_bind_argument_count = MIN(bind_argument_count, callable_argument_count);
			const int checked_bind_start = callable_argument_count - checked_bind_argument_count;

			for (int i = 0; i < checked_bind_argument_count; i++) {
				validate_argument_against_type(p_base_type.method_parameter_types[checked_bind_start + i],
						bind_array->elements[i], i + 1, function_name, p_call);
			}

			if (bind_argument_count > callable_argument_count) {
				return_type = over_bound_callable_type(p_base_type);
			} else {
				Vector<BSParser::DataType> remaining_parameter_types;
				const int remaining_argument_count = callable_argument_count - bind_argument_count;
				for (int i = 0; i < remaining_argument_count; i++) {
					remaining_parameter_types.push_back(p_base_type.method_parameter_types[i]);
				}

				Vector<const BSParser::ExpressionNode *> bound_arguments;
				for (BSParser::ExpressionNode *element : bind_array->elements) {
					bound_arguments.push_back(element);
				}

				int remaining_default_arg_count = 0;
				Vector<int> default_survival_extra_argument_counts;
				default_survival_for_bind(bound_arguments, checked_bind_start, remaining_argument_count,
						remaining_default_arg_count, default_survival_extra_argument_counts);

				return_type = transformed_callable_type(p_base_type, remaining_parameter_types, remaining_default_arg_count, false);
				for (int extra_allowed_argument_count : default_survival_extra_argument_counts) {
					return_type.method_extra_allowed_argument_counts.push_back(extra_allowed_argument_count);
				}
				preserve_extra_allowed_argument_counts(bound_arguments, return_type);
			}
		}

		p_call->set_datatype(return_type);
		return true;
	}

	if (is_callable_unbind) {
		List<BSParser::DataType> unbind_par_types;
		unbind_par_types.push_back(analyzer->type_from_property(PropertyInfo(Variant::INT, "argcount"), true));
		validate_call_arg(unbind_par_types, 0, false, p_call);

		BSParser::DataType return_type = plain_callable_type();
		return_type.signature_is_async = p_base_type.signature_is_async;

		if (p_call->arguments.size() == 1) {
			const BSParser::ExpressionNode *unbind_count_arg = p_call->arguments[0];
			if (unbind_count_arg != nullptr && unbind_count_arg->is_constant && unbind_count_arg->reduced_value.get_type() == Variant::INT) {
				const int64_t unbind_argument_count = unbind_count_arg->reduced_value;
				if (unbind_argument_count <= 0) {
					analyzer->push_error("Amount of \"unbind()\" arguments must be 1 or greater.", unbind_count_arg);
				} else {
					const int unbind_count = int(unbind_argument_count);
					Vector<BSParser::DataType> expanded_parameter_types = p_base_type.method_parameter_types;
					const BSParser::DataType variant_type = analyzer->type_from_property(PropertyInfo(Variant::NIL, ""), true);
					for (int i = 0; i < unbind_count; i++) {
						expanded_parameter_types.push_back(variant_type);
					}
					return_type = transformed_callable_type(p_base_type, expanded_parameter_types, p_base_type.method_info.default_arguments.size(), is_callable_vararg);
					return_type.method_unbound_argument_count = p_base_type.method_unbound_argument_count + unbind_count;
					for (int extra_allowed_argument_count : p_base_type.method_extra_allowed_argument_counts) {
						return_type.method_extra_allowed_argument_counts.push_back(extra_allowed_argument_count + unbind_count);
					}
				}
			}
		}

		p_call->set_datatype(return_type);
		return true;
	}

	return false;
}

} // namespace barista_script
