/**************************************************************************/
/*  bs_byte_codegen.h                                                     */
/*                                                                        */
/*  The byte-code back end: fills a BSFunction's tables in memory.        */
/*  Provenance: fs_byte_codegen.h / .cpp @ c9d5e35, without serialization */
/*  and without the validated-pointer tables the generic paths replace.   */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_codegen.h"
#include "bs_function.h"
#include "bs_platform.h"

namespace barista_script {

/**
 * Emits the in-memory function the virtual machine runs.
 *
 * There is no serialized form: the compiled function lives for as long as the script that owns it
 * and is rebuilt from source on reload, so nothing here writes bytes to disk.
 *
 * A construct the back end cannot lower sets the error and stops producing a function, which is
 * what `write_end()` reports by returning null. Every writer that cannot be lowered yet takes that
 * route rather than emitting an approximation.
 */
class BSByteCodeGenerator final : public BSCodeGenerator {
	struct StackSlot {
		Variant::Type type = Variant::NIL;
		Vector<int> bytecode_indices;

		StackSlot() = default;
		explicit StackSlot(Variant::Type p_type) :
				type(p_type) {}
	};

	BSFunction *function = nullptr;
	bool ended = false;
	String error;

	Vector<int> opcodes;

	Vector<StackSlot> locals;
	HashSet<int> dirty_locals;

	Vector<StackSlot> temporaries;
	List<int> used_temporaries;
	HashSet<int> temporaries_pending_clear;
	HashMap<int, List<int>> temporaries_pool;

	int max_locals = 0;
	int current_line = 0;
	int instruction_arguments_max = 0;
	int optional_parameter_count = 0;

	// Variant is not hashable by the default hasher; the constant pool keys by the value's own hash
	// and compares by value identity.
	HashMap<Variant, int, VariantHasher, VariantComparator> constant_map;
	HashMap<StringName, int> name_map;
	Vector<BSMethodBindHandle> method_binds;
	Vector<BSFunction *> lambda_table;

	// Nested control flow patches its own jumps, so each of these is a stack.
	List<int> block_local_counts;
	List<int> if_jump_addresses;
	List<int> for_jump_addresses;
	List<Address> for_counter_variables;
	List<Address> for_container_variables;
	List<Address> for_range_from_variables;
	List<Address> for_range_to_variables;
	List<Address> for_range_step_variables;
	List<int> while_jump_addresses;
	List<int> continue_addresses;
	List<int> logic_operator_jump_positions;
	List<Address> ternary_result;
	List<int> ternary_jump_fail_positions;
	List<int> ternary_jump_skip_positions;
	List<List<int>> breaks_to_patch;

	/** Refuses an index the frozen 24-bit address encoding cannot carry. */
	bool check_address_fits(int p_index, const char *p_what);
	int address_of(const Address &p_address);
	int get_constant_position(const Variant &p_constant);
	int get_name_position(const StringName &p_name);
	int get_method_bind_position(const BSMethodBindHandle &p_method);
	int get_lambda_position(BSFunction *p_lambda);

	void append_opcode(BSFunction::Opcode p_code) { opcodes.push_back(p_code); }
	void append_opcode_and_argument_count(BSFunction::Opcode p_code, int p_argument_count);
	void append(int p_value) { opcodes.push_back(p_value); }
	void append(const Address &p_address) { opcodes.push_back(address_of(p_address)); }
	void append(const StringName &p_name) { opcodes.push_back(get_name_position(p_name)); }
	void patch_jump(int p_address) { opcodes.write[p_address] = opcodes.size(); }

	/** Records that the back end refuses `p_what`; the first refusal is the one reported. */
	void refuse(const String &p_what);

public:
	BSByteCodeGenerator() = default;
	~BSByteCodeGenerator() override;

	uint32_t add_parameter(const StringName &p_name, bool p_is_optional, const BSParser::DataType &p_slot_type, const BSParser::DataType &p_validation_type) override;
	uint32_t add_local(const StringName &p_name, const BSParser::DataType &p_type) override;
	uint32_t add_local_constant(const StringName &p_name, const Variant &p_constant) override;
	uint32_t add_or_get_constant(const Variant &p_constant) override;
	uint32_t add_or_get_name(const StringName &p_name) override;
	uint32_t add_temporary(const BSParser::DataType &p_type) override;
	void pop_temporary() override;
	void clear_temporaries() override;
	void clear_address(const Address &p_address) override;
	bool is_local_dirty(const Address &p_address) const override;

	void start_parameters() override;
	void end_parameters() override;
	void start_block() override;
	void end_block() override;

	void write_start(BaristaScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const BSParser::DataType &p_return_type) override;
	BSFunction *write_end() override;

	void set_signature(const String &p_signature) override;
	void set_initial_line(int p_line) override;

	void write_type_adjust(const Address &p_target, Variant::Type p_new_type) override;
	void write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand) override;
	void write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand, const Address &p_right_operand) override;
	void write_type_test(const Address &p_target, const Address &p_source, const BSParser::DataType &p_type) override;
	void write_type_test_enum(const Address &p_target, const Address &p_source, const PackedInt64Array &p_declared_values, bool p_is_tagged_union) override;
	void write_type_test_enum_case(const Address &p_target, const Address &p_source, int p_tag, const Vector<Address> &p_binds) override;
	void write_and_left_operand(const Address &p_left_operand) override;
	void write_and_right_operand(const Address &p_right_operand) override;
	void write_end_and(const Address &p_target) override;
	void write_or_left_operand(const Address &p_left_operand) override;
	void write_or_right_operand(const Address &p_right_operand) override;
	void write_end_or(const Address &p_target) override;
	void write_start_ternary(const Address &p_target) override;
	void write_ternary_condition(const Address &p_condition) override;
	void write_ternary_true_expr(const Address &p_expr) override;
	void write_ternary_false_expr(const Address &p_expr) override;
	void write_end_ternary() override;
	void write_set(const Address &p_target, const Address &p_index, const Address &p_source) override;
	void write_get(const Address &p_target, const Address &p_index, const Address &p_source) override;
	void write_set_named(const Address &p_target, const StringName &p_name, const Address &p_source) override;
	void write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) override;
	void write_set_member(const Address &p_value, const StringName &p_name) override;
	void write_get_member(const Address &p_target, const StringName &p_name) override;
	void write_set_static_variable(const Address &p_value, const Address &p_class, int p_index) override;
	void write_get_static_variable(const Address &p_target, const Address &p_class, int p_index) override;
	void write_assign(const Address &p_target, const Address &p_source) override;
	void write_assign_with_conversion(const Address &p_target, const Address &p_source) override;
	void write_assign_typed_parameter(const Address &p_target, const Address &p_source, int p_member_index) override;
	void write_assign_typed_script_dynamic(const Address &p_target, const Address &p_source, const Address &p_type_source) override;
	void write_validate_call_argument(const Address &p_target, const Address &p_source, const BSParser::DataType &p_expected_type, const StringName &p_callee_name, int p_argument_index) override;
	void write_assign_typed_tuple(const Address &p_target, const Address &p_source, const BSParser::DataType &p_expected_type) override;
	void write_assign_typed_union(const Address &p_target, const Address &p_source, const BSParser::DataType &p_expected_type) override;
	void write_assign_typed_array_convert(const Address &p_target, const Address &p_source) override;
	void write_assign_typed_dictionary_convert(const Address &p_target, const Address &p_source) override;
	void write_assign_null(const Address &p_target) override;
	void write_assign_true(const Address &p_target) override;
	void write_assign_false(const Address &p_target) override;
	void write_assign_default_parameter(const Address &p_destination, const Address &p_source, bool p_use_conversion) override;
	void write_store_global(const Address &p_destination, int p_global_index, const StringName &p_global_name) override;
	void write_store_named_global(const Address &p_destination, const StringName &p_global) override;
	void write_cast(const Address &p_target, const Address &p_source, const BSParser::DataType &p_type) override;
	void write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_call_async(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_super_call_async(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_enum_call(const Address &p_target, const Address &p_base, const Vector<Address> &p_arguments,
			const StringName &p_owner_script_path, const StringName &p_owner_class, const StringName &p_enum_type,
			const StringName &p_function_name, bool p_static, bool p_async) override;
	void write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) override;
	void write_call_barista_script_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) override;
	void write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) override;
	void write_call_builtin_type_static(const Address &p_target, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) override;
	void write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments) override;
	void write_call_native_static_validated(const Address &p_target, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) override;
	void write_call_method_bind(const Address &p_target, const Address &p_base, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) override;
	void write_call_method_bind_validated(const Address &p_target, const Address &p_base, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) override;
	void write_call_self(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_call_self_async(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_call_script_function(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) override;
	void write_lambda(const Address &p_target, BSFunction *p_function, const Vector<Address> &p_captures, bool p_use_self) override;
	void write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) override;
	void write_construct_array(const Address &p_target, const Vector<Address> &p_arguments) override;
	void write_construct_typed_array(const Address &p_target, const BSParser::DataType &p_element_type, const Vector<Address> &p_arguments) override;
	void write_construct_tuple(const Address &p_target, const Vector<Address> &p_arguments) override;
	void write_construct_dictionary(const Address &p_target, const Vector<Address> &p_arguments) override;
	void write_construct_typed_dictionary(const Address &p_target, const BSParser::DataType &p_key_type, const BSParser::DataType &p_value_type, const Vector<Address> &p_arguments) override;
	void write_load_static_self_class(const Address &p_target) override;
	void write_await(const Address &p_target, const Address &p_operand) override;
	void write_if(const Address &p_condition) override;
	void write_else() override;
	void write_endif() override;
	void write_jump_if_shared(const Address &p_value) override;
	void write_end_jump_if_shared() override;
	void start_for(const BSParser::DataType &p_iterator_type, const BSParser::DataType &p_list_type, bool p_is_range) override;
	void write_for_list_assignment(const Address &p_list) override;
	void write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) override;
	void write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) override;
	void write_endfor(bool p_is_range) override;
	void start_while_condition() override;
	void write_while(const Address &p_condition) override;
	void write_endwhile() override;
	void write_break() override;
	void write_continue() override;
	void write_breakpoint() override;
	void write_newline(int p_line) override;
	void write_return(const Address &p_return_value) override;
	void write_assert(const Address &p_test, const Address &p_message) override;

	bool has_error() const override { return !error.is_empty(); }
	String get_error() const override { return error; }
};

} // namespace barista_script
