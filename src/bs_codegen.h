/**************************************************************************/
/*  bs_codegen.h                                                          */
/*                                                                        */
/*  The emitter interface every BaristaScript back end implements.        */
/*  Provenance: fs_codegen.h:41-222 @ c9d5e35, minus the D1 numeric tower  */
/*  and the M5 specialization writers.                                    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_function.h"
#include "bs_parser.h"
#include "bs_platform.h"

namespace barista_script {

class BaristaScript;

/**
 * The complete emitter surface, frozen for the whole runtime milestone.
 *
 * The compiler talks to a back end only through this interface, so the compiler families can be
 * written against a signature set that does not move. A back end that cannot emit a construct
 * reports it through `BSCodeGenerator::has_error()` / `get_error()` rather than emitting something
 * approximate: an emitter that silently drops a construct produces a function that runs and is
 * wrong, which is the one failure mode a compiled language cannot recover from.
 *
 * `MethodBind` has no GDExtension counterpart -- an extension cannot obtain the engine's method
 * bind pointers -- so the two method-bind writers take `BSMethodBindHandle`, a class/method name
 * pair the runtime resolves through the dynamic call path. They exist so the interface keeps the
 * shape a validated fast path would need; nothing emits them today.
 */
class BSCodeGenerator {
public:
	struct Address {
		enum AddressMode {
			SELF,
			CLASS,
			MEMBER,
			CONSTANT,
			LOCAL_VARIABLE,
			FUNCTION_PARAMETER,
			TEMPORARY,
			NIL,
		};
		AddressMode mode = NIL;
		uint32_t address = 0;
		BSParser::DataType type;

		Address() {}
		Address(AddressMode p_mode, const BSParser::DataType &p_type = BSParser::DataType()) {
			mode = p_mode;
			type = p_type;
		}
		Address(AddressMode p_mode, uint32_t p_address, const BSParser::DataType &p_type = BSParser::DataType()) {
			mode = p_mode;
			address = p_address;
			type = p_type;
		}
	};

	// `p_slot_type` types the parameter's stack slot and its codegen address; `p_validation_type` is
	// what the call boundary validates an incoming argument against. They differ only for a tuple,
	// whose slot carrier erases to a bare Array while its declared shape -- arity and element types --
	// is what the boundary has to enforce. Handing the shape to the slot instead would change how the
	// body is lowered, since every `write_assign*` dispatches on the address type's kind.
	virtual uint32_t add_parameter(const StringName &p_name, bool p_is_optional, const BSParser::DataType &p_slot_type, const BSParser::DataType &p_validation_type) = 0;
	virtual uint32_t add_local(const StringName &p_name, const BSParser::DataType &p_type) = 0;
	virtual uint32_t add_local_constant(const StringName &p_name, const Variant &p_constant) = 0;
	virtual uint32_t add_or_get_constant(const Variant &p_constant) = 0;
	virtual uint32_t add_or_get_name(const StringName &p_name) = 0;
	virtual uint32_t add_temporary(const BSParser::DataType &p_type) = 0;
	virtual void pop_temporary() = 0;
	virtual void clear_temporaries() = 0;
	virtual void clear_address(const Address &p_address) = 0;
	virtual bool is_local_dirty(const Address &p_address) const = 0;

	virtual void start_parameters() = 0;
	virtual void end_parameters() = 0;

	virtual void start_block() = 0;
	virtual void end_block() = 0;

	virtual void write_start(BaristaScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const BSParser::DataType &p_return_type) = 0;
	virtual BSFunction *write_end() = 0;

	virtual void set_signature(const String &p_signature) = 0;
	virtual void set_initial_line(int p_line) = 0;

	virtual void write_type_adjust(const Address &p_target, Variant::Type p_new_type) = 0;
	virtual void write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand) = 0;
	virtual void write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand, const Address &p_right_operand) = 0;
	virtual void write_type_test(const Address &p_target, const Address &p_source, const BSParser::DataType &p_type) = 0;
	// Membership test against an enum's declared value set. A tagged union tests the tag of a
	// `[tag, payload...]` value; an int-backed enum tests the integer itself.
	virtual void write_type_test_enum(const Address &p_target, const Address &p_source, const PackedInt64Array &p_declared_values, bool p_is_tagged_union) = 0;
	// Tagged-union case test. On a match the payload elements are written to p_binds, which holds one
	// address per payload position (a skipped `_` position gets a scratch address).
	virtual void write_type_test_enum_case(const Address &p_target, const Address &p_source, int p_tag, const Vector<Address> &p_binds) = 0;
	virtual void write_and_left_operand(const Address &p_left_operand) = 0;
	virtual void write_and_right_operand(const Address &p_right_operand) = 0;
	virtual void write_end_and(const Address &p_target) = 0;
	virtual void write_or_left_operand(const Address &p_left_operand) = 0;
	virtual void write_or_right_operand(const Address &p_right_operand) = 0;
	virtual void write_end_or(const Address &p_target) = 0;
	virtual void write_start_ternary(const Address &p_target) = 0;
	virtual void write_ternary_condition(const Address &p_condition) = 0;
	virtual void write_ternary_true_expr(const Address &p_expr) = 0;
	virtual void write_ternary_false_expr(const Address &p_expr) = 0;
	virtual void write_end_ternary() = 0;
	virtual void write_set(const Address &p_target, const Address &p_index, const Address &p_source) = 0;
	virtual void write_get(const Address &p_target, const Address &p_index, const Address &p_source) = 0;
	virtual void write_set_named(const Address &p_target, const StringName &p_name, const Address &p_source) = 0;
	virtual void write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) = 0;
	virtual void write_set_member(const Address &p_value, const StringName &p_name) = 0;
	virtual void write_get_member(const Address &p_target, const StringName &p_name) = 0;
	virtual void write_set_static_variable(const Address &p_value, const Address &p_class, int p_index) = 0;
	virtual void write_get_static_variable(const Address &p_target, const Address &p_class, int p_index) = 0;
	virtual void write_assign(const Address &p_target, const Address &p_source) = 0;
	virtual void write_assign_with_conversion(const Address &p_target, const Address &p_source) = 0;
	// Store into a member typed as a class type parameter (`value: T`), validating the value against
	// the instance's reified type argument at runtime.
	virtual void write_assign_typed_parameter(const Address &p_target, const Address &p_source, int p_member_index) = 0;
	// Store validated against a class named by a runtime value rather than by the declaration: the
	// type to check is read from `p_type_source`, which holds a live class handle.
	virtual void write_assign_typed_script_dynamic(const Address &p_target, const Address &p_source, const Address &p_type_source) = 0;
	// Validates and converts one argument of a statically resolved generic call against the type the
	// call site substituted for the callee's erased method type parameter, before the call is
	// dispatched.
	virtual void write_validate_call_argument(const Address &p_target, const Address &p_source, const BSParser::DataType &p_expected_type, const StringName &p_callee_name, int p_argument_index) = 0;
	// Store into a function-body slot declared as a tuple, validating the value's arity and element
	// types against the declared shape at runtime. The address type of a tuple slot is an erased,
	// untyped Array, so `p_expected_type` is the un-erased `TUPLE` shape the same lowering builds for
	// an `is` test. Nothing is converted: tuple elements are invariant and the value keeps the
	// read-only Array identity its value semantics depend on.
	virtual void write_assign_typed_tuple(const Address &p_target, const Address &p_source, const BSParser::DataType &p_expected_type) = 0;
	// Store into a slot declared as a union, verifying at run time that the value is one of the
	// alternatives. A union has no carrier, so `p_expected_type` is the `UNION` shape itself and the
	// check is the disjunction of its alternatives. Nothing is converted: an alternative reachable
	// only by changing the value's carrier is not an alternative the slot admits, which is the rule
	// the analyzer already applies, so the value the slot accepts is the value the source had.
	virtual void write_assign_typed_union(const Address &p_target, const Address &p_source, const BSParser::DataType &p_expected_type) = 0;
	// Retype a runtime-erased (untyped) array into the concrete typed array of the target,
	// converting each element.
	virtual void write_assign_typed_array_convert(const Address &p_target, const Address &p_source) = 0;
	// Retype a runtime-erased (untyped) dictionary into the concrete typed dictionary of the target,
	// converting each entry.
	virtual void write_assign_typed_dictionary_convert(const Address &p_target, const Address &p_source) = 0;
	virtual void write_assign_null(const Address &p_target) = 0;
	virtual void write_assign_true(const Address &p_target) = 0;
	virtual void write_assign_false(const Address &p_target) = 0;
	virtual void write_assign_default_parameter(const Address &p_destination, const Address &p_source, bool p_use_conversion) = 0;
	virtual void write_store_global(const Address &p_destination, int p_global_index, const StringName &p_global_name) = 0;
	virtual void write_store_named_global(const Address &p_destination, const StringName &p_global) = 0;
	virtual void write_cast(const Address &p_target, const Address &p_source, const BSParser::DataType &p_type) = 0;
	virtual void write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) = 0;
	virtual void write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) = 0;
	virtual void write_enum_call(const Address &p_target, const Address &p_base, const Vector<Address> &p_arguments,
			const StringName &p_owner_script_path, const StringName &p_owner_class, const StringName &p_enum_type,
			const StringName &p_function_name, bool p_static, bool p_async) = 0;
	virtual void write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_barista_script_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_builtin_type_static(const Address &p_target, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_native_static_validated(const Address &p_target, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_method_bind(const Address &p_target, const Address &p_base, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_method_bind_validated(const Address &p_target, const Address &p_base, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_self(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_self_async(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) = 0;
	virtual void write_call_script_function(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) = 0;
	virtual void write_lambda(const Address &p_target, BSFunction *p_function, const Vector<Address> &p_captures, bool p_use_self) = 0;
	virtual void write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) = 0;
	virtual void write_construct_array(const Address &p_target, const Vector<Address> &p_arguments) = 0;
	virtual void write_construct_typed_array(const Address &p_target, const BSParser::DataType &p_element_type, const Vector<Address> &p_arguments) = 0;
	virtual void write_construct_tuple(const Address &p_target, const Vector<Address> &p_arguments) = 0;
	virtual void write_construct_dictionary(const Address &p_target, const Vector<Address> &p_arguments) = 0;
	virtual void write_construct_typed_dictionary(const Address &p_target, const BSParser::DataType &p_key_type, const BSParser::DataType &p_value_type, const Vector<Address> &p_arguments) = 0;
	// Writes the class handle of the frame's exact static receiver. `Self` in an expression position
	// cannot be a constant: one compiled function runs for every receiver it is inherited through.
	virtual void write_load_static_self_class(const Address &p_target) = 0;
	virtual void write_await(const Address &p_target, const Address &p_operand) = 0;
	virtual void write_if(const Address &p_condition) = 0;
	virtual void write_else() = 0;
	virtual void write_endif() = 0;
	virtual void write_jump_if_shared(const Address &p_value) = 0;
	virtual void write_end_jump_if_shared() = 0;
	virtual void start_for(const BSParser::DataType &p_iterator_type, const BSParser::DataType &p_list_type, bool p_is_range) = 0;
	virtual void write_for_list_assignment(const Address &p_list) = 0;
	virtual void write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) = 0;
	virtual void write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) = 0;
	virtual void write_endfor(bool p_is_range) = 0;
	virtual void start_while_condition() = 0; // Used to allow a jump to the expression evaluation.
	virtual void write_while(const Address &p_condition) = 0;
	virtual void write_endwhile() = 0;
	virtual void write_break() = 0;
	virtual void write_continue() = 0;
	virtual void write_breakpoint() = 0;
	virtual void write_newline(int p_line) = 0;
	virtual void write_return(const Address &p_return_value) = 0;
	virtual void write_assert(const Address &p_test, const Address &p_message) = 0;

	/** True once the back end refused a construct; `write_end()` then yields no function. */
	virtual bool has_error() const = 0;
	virtual String get_error() const = 0;

	virtual ~BSCodeGenerator() {}
};

} // namespace barista_script
