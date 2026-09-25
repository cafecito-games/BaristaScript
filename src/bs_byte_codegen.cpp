/**************************************************************************/
/*  bs_byte_codegen.cpp                                                   */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_byte_codegen.h"

#include "barista_script.h"

namespace barista_script {

BSByteCodeGenerator::~BSByteCodeGenerator() {
	if (function != nullptr && !ended) {
		memdelete(function);
		function = nullptr;
	}
}

void BSByteCodeGenerator::refuse(const String &p_what) {
	if (error.is_empty()) {
		error = p_what;
	}
}

bool BSByteCodeGenerator::check_address_fits(int p_index, const char *p_what) {
	// The instruction word packs a 24-bit index beside its address-space tag. An index that does not
	// fit would carry into the tag and name a slot in a different address space, so it is refused
	// rather than truncated. Nothing a real script produces comes close; silent corruption in a
	// frozen encoding is what this rules out.
	if (unlikely(p_index < 0 || p_index > BSFunction::ADDR_MASK)) {
		refuse(vformat("a function with more than %d %s", (int)BSFunction::ADDR_MASK, p_what));
		return false;
	}
	return true;
}

int BSByteCodeGenerator::address_of(const Address &p_address) {
	switch (p_address.mode) {
		case Address::SELF:
			return BSFunction::ADDR_SELF;
		case Address::CLASS:
			return BSFunction::ADDR_CLASS;
		case Address::MEMBER:
			return p_address.address | (BSFunction::ADDR_TYPE_MEMBER << BSFunction::ADDR_BITS);
		case Address::CONSTANT:
			return p_address.address | (BSFunction::ADDR_TYPE_CONSTANT << BSFunction::ADDR_BITS);
		case Address::LOCAL_VARIABLE:
		case Address::FUNCTION_PARAMETER:
			return p_address.address | (BSFunction::ADDR_TYPE_STACK << BSFunction::ADDR_BITS);
		case Address::TEMPORARY:
			// A temporary's stack slot is only known once every local is known, so the position is
			// recorded and patched by `write_end()`.
			temporaries.write[p_address.address].bytecode_indices.push_back(opcodes.size());
			return -1;
		case Address::NIL:
			return BSFunction::ADDR_NIL;
	}
	return BSFunction::ADDR_NIL;
}

int BSByteCodeGenerator::get_constant_position(const Variant &p_constant) {
	if (HashMap<Variant, int, VariantHasher, VariantComparator>::Iterator existing = constant_map.find(p_constant)) {
		return existing->value;
	}
	const int index = constant_map.size();
	check_address_fits(index, "constants");
	constant_map[p_constant] = index;
	return index;
}

int BSByteCodeGenerator::get_name_position(const StringName &p_name) {
	if (HashMap<StringName, int>::Iterator existing = name_map.find(p_name)) {
		return existing->value;
	}
	const int index = name_map.size();
	name_map[p_name] = index;
	return index;
}

int BSByteCodeGenerator::get_method_bind_position(const BSMethodBindHandle &p_method) {
	for (int i = 0; i < method_binds.size(); i++) {
		if (method_binds[i].class_name == p_method.class_name && method_binds[i].method_name == p_method.method_name) {
			return i;
		}
	}
	method_binds.push_back(p_method);
	return method_binds.size() - 1;
}

int BSByteCodeGenerator::get_lambda_position(BSFunction *p_lambda) {
	for (int i = 0; i < lambda_table.size(); i++) {
		if (lambda_table[i] == p_lambda) {
			return i;
		}
	}
	lambda_table.push_back(p_lambda);
	return lambda_table.size() - 1;
}

int BSByteCodeGenerator::get_runtime_type_position(const BSParser::DataType &p_type) {
	BSRuntimeType runtime_type;
	String lowering_error;
	if (!BSRuntimeType::from_data_type(p_type, function != nullptr ? function->script : nullptr,
				runtime_type, lowering_error)) {
		refuse(lowering_error);
		runtime_type = BSRuntimeType();
	}
	for (int i = 0; i < function->runtime_types.size(); i++) {
		if (function->runtime_types[i] == runtime_type) {
			return i;
		}
	}
	function->runtime_types.push_back(runtime_type);
	return function->runtime_types.size() - 1;
}

void BSByteCodeGenerator::append_opcode_and_argument_count(BSFunction::Opcode p_code, int p_argument_count) {
	opcodes.push_back(p_code);
	opcodes.push_back(p_argument_count);
	instruction_arguments_max = MAX(instruction_arguments_max, p_argument_count);
}

uint32_t BSByteCodeGenerator::add_parameter(const StringName &p_name, bool p_is_optional, const BSParser::DataType &p_slot_type, const BSParser::DataType &p_validation_type) {
	function->argument_count++;
	const int type_index = get_runtime_type_position(p_validation_type);
	function->argument_types.push_back(function->runtime_types[type_index]);
	if (p_is_optional) {
		optional_parameter_count++;
	}
	return add_local(p_name, p_slot_type);
}

uint32_t BSByteCodeGenerator::add_rest_parameter(const StringName &p_name, const BSParser::DataType &p_slot_type, const BSParser::DataType &p_validation_type) {
	// `argument_count` is the number of fixed arguments accepted before a tail is collected. One
	// additional type entry records that this function has a rest slot without adding a field to the
	// frozen BSFunction layout or changing the abstract code-generator interface.
	const int type_index = get_runtime_type_position(p_validation_type);
	function->argument_types.push_back(function->runtime_types[type_index]);
	return add_local(p_name, p_slot_type);
}

uint32_t BSByteCodeGenerator::add_local(const StringName &p_name, const BSParser::DataType &p_type) {
	const int stack_position = locals.size() + BSFunction::FIXED_ADDRESSES_MAX;
	check_address_fits(stack_position, "stack slots");
	locals.push_back(StackSlot(p_type.kind == BSParser::DataType::BUILTIN ? p_type.builtin_type : Variant::NIL));
	if (locals.size() > max_locals) {
		max_locals = locals.size();
	}
	return stack_position;
}

uint32_t BSByteCodeGenerator::add_local_constant(const StringName &, const Variant &p_constant) {
	// A local constant has no slot of its own: the compiler records the address and every read of the
	// name becomes a read of the constant pool.
	return add_or_get_constant(p_constant);
}

uint32_t BSByteCodeGenerator::add_or_get_constant(const Variant &p_constant) {
	return get_constant_position(p_constant);
}

uint32_t BSByteCodeGenerator::add_or_get_name(const StringName &p_name) {
	return get_name_position(p_name);
}

uint32_t BSByteCodeGenerator::add_temporary(const BSParser::DataType &p_type) {
	// Reference-counted carriers are never pooled: reusing a slot that still holds an object would
	// keep it alive past the statement that produced it.
	int pool_type = Variant::NIL;
	if (p_type.kind == BSParser::DataType::BUILTIN) {
		switch (p_type.builtin_type) {
			case Variant::OBJECT:
			case Variant::DICTIONARY:
			case Variant::ARRAY:
			case Variant::PACKED_BYTE_ARRAY:
			case Variant::PACKED_INT32_ARRAY:
			case Variant::PACKED_INT64_ARRAY:
			case Variant::PACKED_FLOAT32_ARRAY:
			case Variant::PACKED_FLOAT64_ARRAY:
			case Variant::PACKED_STRING_ARRAY:
			case Variant::PACKED_VECTOR2_ARRAY:
			case Variant::PACKED_VECTOR3_ARRAY:
			case Variant::PACKED_COLOR_ARRAY:
			case Variant::PACKED_VECTOR4_ARRAY:
				break;
			default:
				pool_type = p_type.builtin_type;
				break;
		}
	}
	List<int> &pool = temporaries_pool[pool_type];
	if (pool.is_empty()) {
		pool.push_back(temporaries.size());
		temporaries.push_back(StackSlot((Variant::Type)pool_type));
	}
	const int slot = pool.front()->get();
	pool.pop_front();
	used_temporaries.push_back(slot);
	return slot;
}

void BSByteCodeGenerator::pop_temporary() {
	ERR_FAIL_COND(used_temporaries.is_empty());
	const int slot = used_temporaries.back()->get();
	temporaries_pending_clear.insert(slot);
	temporaries_pool[(int)temporaries[slot].type].push_back(slot);
	used_temporaries.pop_back();
}

void BSByteCodeGenerator::clear_temporaries() {
	for (const int slot : temporaries_pending_clear) {
		clear_address(Address(Address::TEMPORARY, slot));
	}
	temporaries_pending_clear.clear();
}

void BSByteCodeGenerator::clear_address(const Address &p_address) {
	// Clearing gives the slot a value of its own carrier: a declared `int` that was never assigned
	// has to read back as 0, not as the cheap placeholder an untyped slot can take. A pooled
	// temporary keeps the carrier its pool entry was taken for.
	if (p_address.type.kind == BSParser::DataType::BUILTIN &&
			p_address.type.builtin_type == Variant::ARRAY && p_address.type.has_container_element_type(0)) {
		write_construct_typed_array(p_address, p_address.type.get_container_element_type(0), {});
	} else if (p_address.type.kind == BSParser::DataType::BUILTIN &&
			p_address.type.builtin_type == Variant::DICTIONARY && p_address.type.has_container_element_types()) {
		write_construct_typed_dictionary(p_address,
				p_address.type.get_container_element_type_or_variant(0),
				p_address.type.get_container_element_type_or_variant(1), {});
	} else if (p_address.type.kind == BSParser::DataType::BUILTIN && p_address.type.builtin_type != Variant::NIL) {
		write_type_adjust(p_address, p_address.type.builtin_type);
	} else if (p_address.type.kind == BSParser::DataType::NATIVE) {
		// An object slot with nothing in it holds null, not the placeholder an untyped slot takes.
		write_assign_null(p_address);
	} else if (p_address.mode == Address::TEMPORARY && temporaries[p_address.address].type != Variant::NIL) {
		write_type_adjust(p_address, temporaries[p_address.address].type);
	} else {
		// An untyped slot's empty value is null, not false. The two are not interchangeable: a
		// declaration with no initializer is observable (`print(a)` before any assignment), and the
		// language says such a slot reads back as null. Writing `false` would make an unassigned
		// `var a` print `false`, which is what `features/reset_unassigned_variables_in_loops`
		// catches. Provenance: fs_byte_codegen.cpp:3016-3026 `clear_address` @ c9d5e35.
		write_assign_null(p_address);
	}
	if (p_address.mode == Address::LOCAL_VARIABLE) {
		// The slot now holds its own empty value, so it is no longer carrying a previous block's
		// leftovers. Marking it dirty here would be backwards: `is_local_dirty` asks whether the
		// slot still needs clearing, and this is the write that clears it.
		dirty_locals.erase(p_address.address);
	}
}

bool BSByteCodeGenerator::is_local_dirty(const Address &p_address) const {
	return p_address.mode == Address::LOCAL_VARIABLE && dirty_locals.has(p_address.address);
}

void BSByteCodeGenerator::start_parameters() {
	if (optional_parameter_count > 0) {
		append_opcode(BSFunction::OPCODE_JUMP_TO_DEF_ARGUMENT);
		function->default_arguments.push_back(opcodes.size());
	}
}

void BSByteCodeGenerator::end_parameters() {
	function->default_arguments.reverse();
}

void BSByteCodeGenerator::start_block() {
	block_local_counts.push_back(locals.size());
}

void BSByteCodeGenerator::end_block() {
	ERR_FAIL_COND(block_local_counts.is_empty());
	// The slots a block declared are reusable by its siblings; `max_locals` keeps the frame's
	// high-water mark, so the stack is still sized for the deepest block.
	const int surviving = block_local_counts.back()->get();
	// A slot this block is giving back still holds whatever the block last wrote into it. The next
	// declaration to take the slot must therefore clear it before its first read, or an
	// uninitialized `var a` in a later sibling block reads the previous block's value -- which is
	// exactly what `features/reset_uninit_local_vars` observes. Recording the release here is what
	// lets `is_local_dirty` answer that question; the compiler asks it for every declaration with
	// no initializer. Provenance: fs_byte_codegen.h:230-233 `pop_stack_identifiers` @ c9d5e35.
	for (int slot = surviving; slot < locals.size(); slot++) {
		dirty_locals.insert(slot + BSFunction::FIXED_ADDRESSES_MAX);
	}
	locals.resize(surviving);
	block_local_counts.pop_back();
}

void BSByteCodeGenerator::write_start(BaristaScript *p_script, const StringName &p_function_name, bool p_static, Variant p_rpc_config, const BSParser::DataType &p_return_type) {
	function = memnew(BSFunction);
	function->name = p_function_name;
	function->script = p_script;
	function->source = p_script != nullptr ? p_script->get_path() : String();
	function->is_static_function = p_static;
	const int return_type_index = get_runtime_type_position(p_return_type);
	function->return_type = function->runtime_types[return_type_index];
	function->rpc_config = p_rpc_config;
	function->argument_count = 0;
}

BSFunction *BSByteCodeGenerator::write_end() {
	append_opcode(BSFunction::OPCODE_END);

	if (!error.is_empty()) {
		memdelete(function);
		function = nullptr;
		ended = true;
		return nullptr;
	}

	for (int i = 0; i < temporaries.size(); i++) {
		const int stack_index = i + max_locals + BSFunction::FIXED_ADDRESSES_MAX;
		check_address_fits(stack_index, "stack slots");
		for (const int position : temporaries[i].bytecode_indices) {
			opcodes.write[position] = stack_index | (BSFunction::ADDR_TYPE_STACK << BSFunction::ADDR_BITS);
		}
	}

	function->constants.resize(constant_map.size());
	for (const KeyValue<Variant, int> &entry : constant_map) {
		function->constants.write[entry.value] = entry.key;
	}
	function->global_names.resize(name_map.size());
	for (const KeyValue<StringName, int> &entry : name_map) {
		function->global_names.write[entry.value] = entry.key;
	}
	function->methods = method_binds;
	function->lambdas = lambda_table;
	function->code = opcodes;
	function->stack_size = BSFunction::FIXED_ADDRESSES_MAX + max_locals + temporaries.size();
	function->instruction_arguments_size = instruction_arguments_max;

	BSFunction *result = function;
	function = nullptr;
	ended = true;
	return result;
}

void BSByteCodeGenerator::set_signature(const String &p_signature) {
	function->signature = p_signature;
}

void BSByteCodeGenerator::set_initial_line(int p_line) {
	function->initial_line = p_line;
	current_line = p_line;
}

void BSByteCodeGenerator::write_type_adjust(const Address &p_target, Variant::Type p_new_type) {
	switch (p_new_type) {
#define BS_TYPE_ADJUST_CASE(m_type, m_opcode)         \
	case Variant::m_type:                             \
		append_opcode(BSFunction::OPCODE_##m_opcode); \
		break;
		BS_TYPE_ADJUST_CASE(BOOL, TYPE_ADJUST_BOOL)
		BS_TYPE_ADJUST_CASE(INT, TYPE_ADJUST_INT)
		BS_TYPE_ADJUST_CASE(FLOAT, TYPE_ADJUST_FLOAT)
		BS_TYPE_ADJUST_CASE(STRING, TYPE_ADJUST_STRING)
		BS_TYPE_ADJUST_CASE(VECTOR2, TYPE_ADJUST_VECTOR2)
		BS_TYPE_ADJUST_CASE(VECTOR2I, TYPE_ADJUST_VECTOR2I)
		BS_TYPE_ADJUST_CASE(RECT2, TYPE_ADJUST_RECT2)
		BS_TYPE_ADJUST_CASE(RECT2I, TYPE_ADJUST_RECT2I)
		BS_TYPE_ADJUST_CASE(VECTOR3, TYPE_ADJUST_VECTOR3)
		BS_TYPE_ADJUST_CASE(VECTOR3I, TYPE_ADJUST_VECTOR3I)
		BS_TYPE_ADJUST_CASE(TRANSFORM2D, TYPE_ADJUST_TRANSFORM2D)
		BS_TYPE_ADJUST_CASE(VECTOR4, TYPE_ADJUST_VECTOR4)
		BS_TYPE_ADJUST_CASE(VECTOR4I, TYPE_ADJUST_VECTOR4I)
		BS_TYPE_ADJUST_CASE(PLANE, TYPE_ADJUST_PLANE)
		BS_TYPE_ADJUST_CASE(QUATERNION, TYPE_ADJUST_QUATERNION)
		BS_TYPE_ADJUST_CASE(AABB, TYPE_ADJUST_AABB)
		BS_TYPE_ADJUST_CASE(BASIS, TYPE_ADJUST_BASIS)
		BS_TYPE_ADJUST_CASE(TRANSFORM3D, TYPE_ADJUST_TRANSFORM3D)
		BS_TYPE_ADJUST_CASE(PROJECTION, TYPE_ADJUST_PROJECTION)
		BS_TYPE_ADJUST_CASE(COLOR, TYPE_ADJUST_COLOR)
		BS_TYPE_ADJUST_CASE(STRING_NAME, TYPE_ADJUST_STRING_NAME)
		BS_TYPE_ADJUST_CASE(NODE_PATH, TYPE_ADJUST_NODE_PATH)
		BS_TYPE_ADJUST_CASE(RID, TYPE_ADJUST_RID)
		BS_TYPE_ADJUST_CASE(OBJECT, TYPE_ADJUST_OBJECT)
		BS_TYPE_ADJUST_CASE(CALLABLE, TYPE_ADJUST_CALLABLE)
		BS_TYPE_ADJUST_CASE(SIGNAL, TYPE_ADJUST_SIGNAL)
		BS_TYPE_ADJUST_CASE(DICTIONARY, TYPE_ADJUST_DICTIONARY)
		BS_TYPE_ADJUST_CASE(ARRAY, TYPE_ADJUST_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_BYTE_ARRAY, TYPE_ADJUST_PACKED_BYTE_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_INT32_ARRAY, TYPE_ADJUST_PACKED_INT32_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_INT64_ARRAY, TYPE_ADJUST_PACKED_INT64_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_FLOAT32_ARRAY, TYPE_ADJUST_PACKED_FLOAT32_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_FLOAT64_ARRAY, TYPE_ADJUST_PACKED_FLOAT64_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_STRING_ARRAY, TYPE_ADJUST_PACKED_STRING_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_VECTOR2_ARRAY, TYPE_ADJUST_PACKED_VECTOR2_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_VECTOR3_ARRAY, TYPE_ADJUST_PACKED_VECTOR3_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_COLOR_ARRAY, TYPE_ADJUST_PACKED_COLOR_ARRAY)
		BS_TYPE_ADJUST_CASE(PACKED_VECTOR4_ARRAY, TYPE_ADJUST_PACKED_VECTOR4_ARRAY)
#undef BS_TYPE_ADJUST_CASE
		default:
			// A nil slot needs no adjustment; every other carrier has an opcode above.
			write_assign_null(p_target);
			return;
	}
	append(p_target);
}

void BSByteCodeGenerator::write_unary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand) {
	// A unary operator shares the binary opcode, and the engine's evaluator recognizes it by the
	// right operand being nil. Repeating the left operand there would make `not true` read as
	// `true not true`, which has no meaning and is rejected.
	write_binary_operator(p_target, p_operator, p_left_operand, Address(Address::NIL));
}

void BSByteCodeGenerator::write_binary_operator(const Address &p_target, Variant::Operator p_operator, const Address &p_left_operand, const Address &p_right_operand) {
	append_opcode(BSFunction::OPCODE_OPERATOR);
	append(p_left_operand);
	append(p_right_operand);
	append(p_target);
	append((int)p_operator);
}

void BSByteCodeGenerator::write_type_test(const Address &p_target, const Address &p_source, const BSParser::DataType &p_type) {
	const int descriptor = get_runtime_type_position(p_type);
	BSFunction::Opcode opcode = BSFunction::OPCODE_TYPE_TEST_BUILTIN;
	const BSRuntimeType &runtime_type = function->runtime_types[descriptor];
	if (runtime_type.is_typed_array()) {
		opcode = BSFunction::OPCODE_TYPE_TEST_ARRAY;
	} else if (runtime_type.is_typed_dictionary()) {
		opcode = BSFunction::OPCODE_TYPE_TEST_DICTIONARY;
	} else if (runtime_type.kind == BSRuntimeType::NATIVE) {
		opcode = BSFunction::OPCODE_TYPE_TEST_NATIVE;
	} else if (runtime_type.kind == BSRuntimeType::SCRIPT) {
		opcode = BSFunction::OPCODE_TYPE_TEST_SCRIPT;
	} else if (runtime_type.kind != BSRuntimeType::BUILTIN && runtime_type.kind != BSRuntimeType::VARIANT) {
		refuse(vformat(R"(a type test against "%s")", p_type.to_string()));
		return;
	}
	append_opcode(opcode);
	append(p_target);
	append(p_source);
	append(descriptor);
}

void BSByteCodeGenerator::write_type_test_enum(const Address &p_target, const Address &p_source,
		const PackedInt64Array &p_declared_values, bool p_is_tagged_union) {
	if (p_is_tagged_union) {
		refuse("a tagged-union membership test");
		return;
	}
	append_opcode(BSFunction::OPCODE_TYPE_TEST_ENUM);
	append(p_target);
	append(p_source);
	append(Address(Address::CONSTANT, get_constant_position(p_declared_values)));
}

void BSByteCodeGenerator::write_type_test_enum_case(const Address &, const Address &, int, const Vector<Address> &) {
	refuse("a tagged-union case test");
}

void BSByteCodeGenerator::write_and_left_operand(const Address &p_left_operand) {
	append_opcode(BSFunction::OPCODE_JUMP_IF_NOT);
	append(p_left_operand);
	logic_operator_jump_positions.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_and_right_operand(const Address &p_right_operand) {
	append_opcode(BSFunction::OPCODE_JUMP_IF_NOT);
	append(p_right_operand);
	logic_operator_jump_positions.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_end_and(const Address &p_target) {
	append_opcode(BSFunction::OPCODE_ASSIGN_TRUE);
	append(p_target);
	append_opcode(BSFunction::OPCODE_JUMP);
	const int skip = opcodes.size();
	append(0);

	const int right = logic_operator_jump_positions.back()->get();
	logic_operator_jump_positions.pop_back();
	const int left = logic_operator_jump_positions.back()->get();
	logic_operator_jump_positions.pop_back();
	patch_jump(left);
	patch_jump(right);

	append_opcode(BSFunction::OPCODE_ASSIGN_FALSE);
	append(p_target);
	patch_jump(skip);
}

void BSByteCodeGenerator::write_or_left_operand(const Address &p_left_operand) {
	append_opcode(BSFunction::OPCODE_JUMP_IF);
	append(p_left_operand);
	logic_operator_jump_positions.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_or_right_operand(const Address &p_right_operand) {
	append_opcode(BSFunction::OPCODE_JUMP_IF);
	append(p_right_operand);
	logic_operator_jump_positions.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_end_or(const Address &p_target) {
	append_opcode(BSFunction::OPCODE_ASSIGN_FALSE);
	append(p_target);
	append_opcode(BSFunction::OPCODE_JUMP);
	const int skip = opcodes.size();
	append(0);

	const int right = logic_operator_jump_positions.back()->get();
	logic_operator_jump_positions.pop_back();
	const int left = logic_operator_jump_positions.back()->get();
	logic_operator_jump_positions.pop_back();
	patch_jump(left);
	patch_jump(right);

	append_opcode(BSFunction::OPCODE_ASSIGN_TRUE);
	append(p_target);
	patch_jump(skip);
}

void BSByteCodeGenerator::write_start_ternary(const Address &p_target) {
	ternary_result.push_back(p_target);
}

void BSByteCodeGenerator::write_ternary_condition(const Address &p_condition) {
	append_opcode(BSFunction::OPCODE_JUMP_IF_NOT);
	append(p_condition);
	ternary_jump_fail_positions.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_ternary_true_expr(const Address &p_expr) {
	write_assign(ternary_result.back()->get(), p_expr);
	append_opcode(BSFunction::OPCODE_JUMP);
	ternary_jump_skip_positions.push_back(opcodes.size());
	append(0);
	patch_jump(ternary_jump_fail_positions.back()->get());
	ternary_jump_fail_positions.pop_back();
}

void BSByteCodeGenerator::write_ternary_false_expr(const Address &p_expr) {
	write_assign(ternary_result.back()->get(), p_expr);
}

void BSByteCodeGenerator::write_end_ternary() {
	patch_jump(ternary_jump_skip_positions.back()->get());
	ternary_jump_skip_positions.pop_back();
	ternary_result.pop_back();
}

void BSByteCodeGenerator::write_set(const Address &p_target, const Address &p_index, const Address &p_source) {
	append_opcode(BSFunction::OPCODE_SET_KEYED);
	append(p_target);
	append(p_index);
	append(p_source);
}

void BSByteCodeGenerator::write_get(const Address &p_target, const Address &p_index, const Address &p_source) {
	append_opcode(BSFunction::OPCODE_GET_KEYED);
	append(p_source);
	append(p_index);
	append(p_target);
}

void BSByteCodeGenerator::write_set_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	append_opcode(BSFunction::OPCODE_SET_NAMED);
	append(p_target);
	append(p_source);
	append(p_name);
}

void BSByteCodeGenerator::write_get_named(const Address &p_target, const StringName &p_name, const Address &p_source) {
	append_opcode(BSFunction::OPCODE_GET_NAMED);
	append(p_source);
	append(p_target);
	append(p_name);
}

void BSByteCodeGenerator::write_set_member(const Address &p_value, const StringName &p_name) {
	append_opcode(BSFunction::OPCODE_SET_MEMBER);
	append(p_value);
	append(p_name);
}

void BSByteCodeGenerator::write_get_member(const Address &p_target, const StringName &p_name) {
	append_opcode(BSFunction::OPCODE_GET_MEMBER);
	append(p_target);
	append(p_name);
}

void BSByteCodeGenerator::write_set_static_variable(const Address &, const Address &, int) {
	refuse("a store into a static variable");
}

void BSByteCodeGenerator::write_get_static_variable(const Address &, const Address &, int) {
	refuse("a read of a static variable");
}

void BSByteCodeGenerator::write_assign(const Address &p_target, const Address &p_source) {
	// A slot with a declared carrier is stored into through the typed opcode whether or not the
	// analyzer flagged the store as converting. The flag marks where a conversion is *expected*;
	// it is not the only place one is needed, because a value's run-time carrier can differ from
	// the slot's without the declaration saying so -- an `int` member read into a `float` slot is
	// the plain case. An untyped slot takes the value exactly as it is.
	if (!p_target.type.is_variant()) {
		const int descriptor = get_runtime_type_position(p_target.type);
		const BSRuntimeType &runtime_type = function->runtime_types[descriptor];
		BSFunction::Opcode opcode = BSFunction::OPCODE_ASSIGN_TYPED_BUILTIN;
		if (runtime_type.is_typed_array()) {
			opcode = BSFunction::OPCODE_ASSIGN_TYPED_ARRAY;
		} else if (runtime_type.is_typed_dictionary()) {
			opcode = BSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY;
		} else if (runtime_type.kind == BSRuntimeType::NATIVE) {
			opcode = BSFunction::OPCODE_ASSIGN_TYPED_NATIVE;
		} else if (runtime_type.kind == BSRuntimeType::SCRIPT) {
			opcode = BSFunction::OPCODE_ASSIGN_TYPED_SCRIPT;
		}
		append_opcode(opcode);
		append(p_target);
		append(p_source);
		append(descriptor);
		return;
	}
	append_opcode(BSFunction::OPCODE_ASSIGN);
	append(p_target);
	append(p_source);
}

void BSByteCodeGenerator::write_assign_with_conversion(const Address &p_target, const Address &p_source) {
	if (p_target.type.kind == BSParser::DataType::BUILTIN &&
			p_target.type.builtin_type == Variant::ARRAY && p_target.type.has_container_element_type(0)) {
		write_assign_typed_array_convert(p_target, p_source);
		return;
	}
	if (p_target.type.kind == BSParser::DataType::BUILTIN &&
			p_target.type.builtin_type == Variant::DICTIONARY && p_target.type.has_container_element_types()) {
		write_assign_typed_dictionary_convert(p_target, p_source);
		return;
	}
	write_assign(p_target, p_source);
}

void BSByteCodeGenerator::write_assign_typed_parameter(const Address &p_target, const Address &p_source, int) {
	write_assign(p_target, p_source);
}

void BSByteCodeGenerator::write_assign_typed_script_dynamic(const Address &, const Address &, const Address &) {
	refuse("a store checked against a runtime class handle");
}

void BSByteCodeGenerator::write_validate_call_argument(const Address &, const Address &, const BSParser::DataType &, const StringName &, int) {
	refuse("a call-site generic argument check");
}

void BSByteCodeGenerator::write_assign_typed_tuple(const Address &, const Address &, const BSParser::DataType &) {
	refuse("a store into a tuple slot");
}

void BSByteCodeGenerator::write_assign_typed_union(const Address &, const Address &, const BSParser::DataType &) {
	refuse("a store into a union slot");
}

void BSByteCodeGenerator::write_assign_typed_array_convert(const Address &p_target, const Address &p_source) {
	append_opcode(BSFunction::OPCODE_ASSIGN_TYPED_ARRAY_CONVERT);
	append(p_target);
	append(p_source);
	append(get_runtime_type_position(p_target.type));
}

void BSByteCodeGenerator::write_assign_typed_dictionary_convert(const Address &p_target, const Address &p_source) {
	append_opcode(BSFunction::OPCODE_ASSIGN_TYPED_DICTIONARY_CONVERT);
	append(p_target);
	append(p_source);
	append(get_runtime_type_position(p_target.type));
}

void BSByteCodeGenerator::write_assign_null(const Address &p_target) {
	append_opcode(BSFunction::OPCODE_ASSIGN_NULL);
	append(p_target);
}

void BSByteCodeGenerator::write_assign_true(const Address &p_target) {
	append_opcode(BSFunction::OPCODE_ASSIGN_TRUE);
	append(p_target);
}

void BSByteCodeGenerator::write_assign_false(const Address &p_target) {
	append_opcode(BSFunction::OPCODE_ASSIGN_FALSE);
	append(p_target);
}

void BSByteCodeGenerator::write_assign_default_parameter(const Address &p_destination, const Address &p_source, bool p_use_conversion) {
	if (p_use_conversion) {
		write_assign_with_conversion(p_destination, p_source);
	} else {
		write_assign(p_destination, p_source);
	}
	function->default_arguments.push_back(opcodes.size());
}

void BSByteCodeGenerator::write_store_global(const Address &p_destination, int p_global_index, const StringName &) {
	append_opcode(BSFunction::OPCODE_STORE_GLOBAL);
	append(p_destination);
	append(p_global_index);
}

void BSByteCodeGenerator::write_store_named_global(const Address &p_destination, const StringName &p_global) {
	append_opcode(BSFunction::OPCODE_STORE_NAMED_GLOBAL);
	append(p_destination);
	append(p_global);
}

void BSByteCodeGenerator::write_cast(const Address &p_target, const Address &p_source, const BSParser::DataType &p_type) {
	const int descriptor = get_runtime_type_position(p_type);
	const BSRuntimeType &runtime_type = function->runtime_types[descriptor];
	BSFunction::Opcode opcode;
	if (runtime_type.kind == BSRuntimeType::BUILTIN) {
		opcode = BSFunction::OPCODE_CAST_TO_BUILTIN;
	} else if (runtime_type.kind == BSRuntimeType::NATIVE) {
		opcode = BSFunction::OPCODE_CAST_TO_NATIVE;
	} else if (runtime_type.kind == BSRuntimeType::SCRIPT) {
		opcode = BSFunction::OPCODE_CAST_TO_SCRIPT;
	} else if (runtime_type.kind == BSRuntimeType::VARIANT) {
		write_assign(p_target, p_source);
		return;
	} else {
		refuse(vformat(R"(a cast to "%s")", p_type.to_string()));
		return;
	}
	append_opcode(opcode);
	append(p_source);
	append(p_target);
	append(descriptor);
}

void BSByteCodeGenerator::write_call(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	const bool wants_return = p_target.mode != Address::NIL;
	append_opcode_and_argument_count(wants_return ? BSFunction::OPCODE_CALL_RETURN : BSFunction::OPCODE_CALL,
			p_arguments.size() + (wants_return ? 2 : 1));
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_base);
	if (wants_return) {
		append(p_target);
	}
	append(p_arguments.size());
	append(p_function_name);
}

void BSByteCodeGenerator::write_super_call(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CALL_SELF_BASE, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append(p_function_name);
}

void BSByteCodeGenerator::write_call_async(const Address &, const Address &, const StringName &, const Vector<Address> &) {
	refuse("an awaited call");
}

void BSByteCodeGenerator::write_super_call_async(const Address &, const StringName &, const Vector<Address> &) {
	refuse("an awaited super call");
}

void BSByteCodeGenerator::write_enum_call(const Address &, const Address &, const Vector<Address> &,
		const StringName &, const StringName &, const StringName &, const StringName &, bool, bool) {
	refuse("an enum function call");
}

void BSByteCodeGenerator::write_call_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CALL_UTILITY, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append(p_function);
}

void BSByteCodeGenerator::write_call_barista_script_utility(const Address &p_target, const StringName &p_function, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CALL_BARISTA_SCRIPT_UTILITY, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append(p_function);
}

void BSByteCodeGenerator::write_call_builtin_type(const Address &p_target, const Address &p_base, Variant::Type, const StringName &p_method, const Vector<Address> &p_arguments) {
	// A builtin method reached through the generic call path is the same dispatch an ordinary call
	// makes on a builtin receiver, so it shares the opcode.
	write_call(p_target, p_base, p_method, p_arguments);
}

void BSByteCodeGenerator::write_call_builtin_type_static(const Address &p_target, Variant::Type p_type, const StringName &p_method, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CALL_BUILTIN_STATIC, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append((int)p_type);
	append(p_method);
}

void BSByteCodeGenerator::write_call_native_static(const Address &p_target, const StringName &p_class, const StringName &p_method, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CALL_NATIVE_STATIC, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append(p_class);
	append(p_method);
}

void BSByteCodeGenerator::write_call_native_static_validated(const Address &, const BSMethodBindHandle &, const Vector<Address> &) {
	refuse("a validated static native call");
}

void BSByteCodeGenerator::write_call_method_bind(const Address &p_target, const Address &p_base, const BSMethodBindHandle &p_method, const Vector<Address> &p_arguments) {
	const bool wants_return = p_target.mode != Address::NIL;
	append_opcode_and_argument_count(wants_return ? BSFunction::OPCODE_CALL_METHOD_BIND_RET : BSFunction::OPCODE_CALL_METHOD_BIND,
			p_arguments.size() + (wants_return ? 2 : 1));
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_base);
	if (wants_return) {
		append(p_target);
	}
	append(p_arguments.size());
	append(get_method_bind_position(p_method));
}

void BSByteCodeGenerator::write_call_method_bind_validated(const Address &, const Address &, const BSMethodBindHandle &, const Vector<Address> &) {
	refuse("a validated method-bind call");
}

void BSByteCodeGenerator::write_call_self(const Address &p_target, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	write_call(p_target, Address(Address::SELF), p_function_name, p_arguments);
}

void BSByteCodeGenerator::write_call_self_async(const Address &, const StringName &, const Vector<Address> &) {
	refuse("an awaited call on self");
}

void BSByteCodeGenerator::write_call_script_function(const Address &p_target, const Address &p_base, const StringName &p_function_name, const Vector<Address> &p_arguments) {
	write_call(p_target, p_base, p_function_name, p_arguments);
}

void BSByteCodeGenerator::write_lambda(const Address &p_target, BSFunction *p_function, const Vector<Address> &p_captures, bool p_use_self) {
	append_opcode_and_argument_count(p_use_self ? BSFunction::OPCODE_CREATE_SELF_LAMBDA : BSFunction::OPCODE_CREATE_LAMBDA,
			p_captures.size() + 1);
	for (const Address &capture : p_captures) {
		append(capture);
	}
	append(p_target);
	append(p_captures.size());
	append(get_lambda_position(p_function));
}

void BSByteCodeGenerator::write_construct(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CONSTRUCT, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append((int)p_type);
}

void BSByteCodeGenerator::write_construct_array(const Address &p_target, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CONSTRUCT_ARRAY, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
}

void BSByteCodeGenerator::write_construct_typed_array(const Address &p_target, const BSParser::DataType &p_element_type, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CONSTRUCT_TYPED_ARRAY, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size());
	append(get_runtime_type_position(p_element_type));
}

void BSByteCodeGenerator::write_construct_tuple(const Address &, const Vector<Address> &) {
	refuse("a tuple literal");
}

void BSByteCodeGenerator::write_construct_dictionary(const Address &p_target, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CONSTRUCT_DICTIONARY, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size() / 2);
}

void BSByteCodeGenerator::write_construct_typed_dictionary(const Address &p_target, const BSParser::DataType &p_key_type, const BSParser::DataType &p_value_type, const Vector<Address> &p_arguments) {
	append_opcode_and_argument_count(BSFunction::OPCODE_CONSTRUCT_TYPED_DICTIONARY, p_arguments.size() + 1);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	append(p_target);
	append(p_arguments.size() / 2);
	append(get_runtime_type_position(p_key_type));
	append(get_runtime_type_position(p_value_type));
}

void BSByteCodeGenerator::write_load_static_self_class(const Address &p_target) {
	append_opcode(BSFunction::OPCODE_LOAD_STATIC_SELF_CLASS);
	append(p_target);
}

void BSByteCodeGenerator::write_await(const Address &, const Address &) {
	refuse("an await expression");
}

void BSByteCodeGenerator::write_if(const Address &p_condition) {
	append_opcode(BSFunction::OPCODE_JUMP_IF_NOT);
	append(p_condition);
	if_jump_addresses.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_else() {
	append_opcode(BSFunction::OPCODE_JUMP);
	const int else_start = opcodes.size();
	append(0);
	patch_jump(if_jump_addresses.back()->get());
	if_jump_addresses.pop_back();
	if_jump_addresses.push_back(else_start);
}

void BSByteCodeGenerator::write_endif() {
	patch_jump(if_jump_addresses.back()->get());
	if_jump_addresses.pop_back();
}

void BSByteCodeGenerator::write_jump_if_shared(const Address &p_value) {
	append_opcode(BSFunction::OPCODE_JUMP_IF_SHARED);
	append(p_value);
	if_jump_addresses.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_end_jump_if_shared() {
	patch_jump(if_jump_addresses.back()->get());
	if_jump_addresses.pop_back();
}

void BSByteCodeGenerator::start_for(const BSParser::DataType &p_iterator_type, const BSParser::DataType &, bool p_is_range) {
	// An integer range's three bounds are integers by definition of the loop form, so their slots
	// declare that carrier. Declaring it is what performs the conversion: a typed slot is stored
	// into through the converting assign, so `range(5.2)` and `range(n)` for a float `n` reach the
	// loop as the integers the form takes, instead of failing at the first instruction that reads
	// them. Provenance: fs_byte_codegen.cpp `start_for` @ c9d5e35, which types the same three slots.
	BSParser::DataType range_bound;
	range_bound.kind = BSParser::DataType::BUILTIN;
	range_bound.builtin_type = Variant::INT;
	range_bound.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	const BSParser::DataType bound_type = p_is_range ? range_bound : BSParser::DataType();

	const Address counter(Address::LOCAL_VARIABLE, add_local("@counter_position", bound_type), bound_type);
	const Address container(Address::LOCAL_VARIABLE,
			add_local(p_is_range ? "@range_from" : "@container_position", bound_type), bound_type);
	for_counter_variables.push_back(counter);
	for_container_variables.push_back(container);
	if (p_is_range) {
		for_range_from_variables.push_back(container);
		for_range_to_variables.push_back(Address(Address::LOCAL_VARIABLE, add_local("@range_to", range_bound), range_bound));
		for_range_step_variables.push_back(Address(Address::LOCAL_VARIABLE, add_local("@range_step", range_bound), range_bound));
	}
	breaks_to_patch.push_back(List<int>());
	(void)p_iterator_type;
}

void BSByteCodeGenerator::write_for_list_assignment(const Address &p_list) {
	write_assign(for_container_variables.back()->get(), p_list);
}

void BSByteCodeGenerator::write_for_range_assignment(const Address &p_from, const Address &p_to, const Address &p_step) {
	write_assign(for_range_from_variables.back()->get(), p_from);
	write_assign(for_range_to_variables.back()->get(), p_to);
	write_assign(for_range_step_variables.back()->get(), p_step);
}

void BSByteCodeGenerator::write_for(const Address &p_variable, bool p_use_conversion, bool p_is_range) {
	// The loop is laid out as begin, a jump over the advance step, the advance step, then the body.
	// `continue` targets the advance step, and both the begin's empty-sequence jump and the advance
	// step's exhausted jump are patched to the loop's exit by `write_endfor`.
	//
	// A loop variable whose declared carrier differs from the element's is filled through a hidden
	// slot and converted at the top of the body. Both ways into the body -- the first element and
	// every later one -- arrive at that point, so one conversion covers the whole loop.
	const Address element = p_use_conversion
			? Address(Address::LOCAL_VARIABLE, add_local("@loop_element", BSParser::DataType()), BSParser::DataType())
			: p_variable;
	if (p_is_range) {
		append_opcode(BSFunction::OPCODE_ITERATE_BEGIN_RANGE);
		append(for_counter_variables.back()->get());
		append(for_range_from_variables.back()->get());
		append(for_range_to_variables.back()->get());
		append(for_range_step_variables.back()->get());
		append(element);
		for_jump_addresses.push_back(opcodes.size());
		append(0);
		append_opcode(BSFunction::OPCODE_JUMP);
		append(opcodes.size() + 7);

		continue_addresses.push_back(opcodes.size());
		append_opcode(BSFunction::OPCODE_ITERATE_RANGE);
		append(for_counter_variables.back()->get());
		append(for_range_to_variables.back()->get());
		append(for_range_step_variables.back()->get());
		append(element);
		for_jump_addresses.push_back(opcodes.size());
		append(0);
	} else {
		append_opcode(BSFunction::OPCODE_ITERATE_BEGIN);
		append(for_counter_variables.back()->get());
		append(for_container_variables.back()->get());
		append(element);
		for_jump_addresses.push_back(opcodes.size());
		append(0);
		append_opcode(BSFunction::OPCODE_JUMP);
		append(opcodes.size() + 6);

		continue_addresses.push_back(opcodes.size());
		append_opcode(BSFunction::OPCODE_ITERATE);
		append(for_counter_variables.back()->get());
		append(for_container_variables.back()->get());
		append(element);
		for_jump_addresses.push_back(opcodes.size());
		append(0);
	}
	if (p_use_conversion) {
		write_assign_with_conversion(p_variable, element);
	}
}

void BSByteCodeGenerator::write_endfor(bool p_is_range) {
	append_opcode(BSFunction::OPCODE_JUMP);
	append(continue_addresses.back()->get());

	for (int i = 0; i < 2; i++) {
		patch_jump(for_jump_addresses.back()->get());
		for_jump_addresses.pop_back();
	}
	for (const int position : breaks_to_patch.back()->get()) {
		opcodes.write[position] = opcodes.size();
	}
	breaks_to_patch.pop_back();

	continue_addresses.pop_back();
	for_counter_variables.pop_back();
	for_container_variables.pop_back();
	if (p_is_range) {
		for_range_from_variables.pop_back();
		for_range_to_variables.pop_back();
		for_range_step_variables.pop_back();
	}
}

void BSByteCodeGenerator::start_while_condition() {
	continue_addresses.push_back(opcodes.size());
	breaks_to_patch.push_back(List<int>());
}

void BSByteCodeGenerator::write_while(const Address &p_condition) {
	append_opcode(BSFunction::OPCODE_JUMP_IF_NOT);
	append(p_condition);
	while_jump_addresses.push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_endwhile() {
	append_opcode(BSFunction::OPCODE_JUMP);
	append(continue_addresses.back()->get());
	patch_jump(while_jump_addresses.back()->get());
	while_jump_addresses.pop_back();

	for (const int position : breaks_to_patch.back()->get()) {
		opcodes.write[position] = opcodes.size();
	}
	breaks_to_patch.pop_back();
	continue_addresses.pop_back();
}

void BSByteCodeGenerator::write_break() {
	append_opcode(BSFunction::OPCODE_JUMP);
	breaks_to_patch.back()->get().push_back(opcodes.size());
	append(0);
}

void BSByteCodeGenerator::write_continue() {
	append_opcode(BSFunction::OPCODE_JUMP);
	append(continue_addresses.back()->get());
}

void BSByteCodeGenerator::write_breakpoint() {
	append_opcode(BSFunction::OPCODE_BREAKPOINT);
}

void BSByteCodeGenerator::write_newline(int p_line) {
	append_opcode(BSFunction::OPCODE_LINE);
	append(p_line);
	current_line = p_line;
}

void BSByteCodeGenerator::write_return(const Address &p_return_value) {
	if (!function->return_type.has_type()) {
		append_opcode(BSFunction::OPCODE_RETURN);
		append(p_return_value);
		return;
	}
	int descriptor = -1;
	for (int i = 0; i < function->runtime_types.size(); i++) {
		if (function->runtime_types[i] == function->return_type) {
			descriptor = i;
			break;
		}
	}
	if (descriptor < 0) {
		refuse("a return type missing from the runtime descriptor table");
		return;
	}
	BSFunction::Opcode opcode = BSFunction::OPCODE_RETURN_TYPED_BUILTIN;
	if (function->return_type.is_typed_array()) {
		opcode = BSFunction::OPCODE_RETURN_TYPED_ARRAY;
	} else if (function->return_type.is_typed_dictionary()) {
		opcode = BSFunction::OPCODE_RETURN_TYPED_DICTIONARY;
	} else if (function->return_type.kind == BSRuntimeType::NATIVE) {
		opcode = BSFunction::OPCODE_RETURN_TYPED_NATIVE;
	} else if (function->return_type.kind == BSRuntimeType::SCRIPT) {
		opcode = BSFunction::OPCODE_RETURN_TYPED_SCRIPT;
	}
	append_opcode(opcode);
	append(p_return_value);
	append(descriptor);
}

void BSByteCodeGenerator::write_assert(const Address &p_test, const Address &p_message) {
	append_opcode(BSFunction::OPCODE_ASSERT);
	append(p_test);
	append(p_message);
	append(p_message.mode == Address::NIL ? 0 : 1);
}

} // namespace barista_script
