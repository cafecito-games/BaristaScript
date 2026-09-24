/**************************************************************************/
/*  bs_vm_ops_iterators.inc                                               */
/*                                                                        */
/*  Iteration handlers. Included inside the dispatch switch in bs_vm.cpp,  */
/*  which owns the operand-reading macros and the exit labels.            */
/*                                                                        */
/*  A loop is laid out as begin, a jump over the advance step, the        */
/*  advance step, then the body: the begin falls through to the jump on   */
/*  a non-empty sequence, `continue` targets the advance step, and both   */
/*  the begin and the advance step jump to the loop's exit when the       */
/*  sequence is empty or exhausted.                                      */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

// The 40 carrier-specific iteration opcodes -- `OPCODE_ITERATE_BEGIN_INT` through
// `OPCODE_ITERATE_PACKED_VECTOR4_ARRAY` -- deliberately have no handler in M4, and reaching one is
// the frozen "opcode not implemented" error rather than a silent fallback.
//
// They are not a gap in behaviour. Each is a specialization of the four handlers below for a
// container whose carrier the compiler already knows: the generic pair goes through
// `Variant::iter_init`/`iter_next`/`iter_get`, which is the same traversal the engine performs for
// the typed ones, so every sequence the language can iterate is iterated correctly here. What the
// specializations buy is speed, by skipping the Variant dispatch, and nothing else.
//
// They stay unemitted because emitting one means the emitter has to commit to a container's carrier
// at compile time, which means typing the loop's container slot from the analyzed list type -- and
// that slot type is exactly what the typed-container work (#246) is still deciding. Choosing it
// twice, once here and once there, is how two type models get built. When that lands, selecting a
// specialized opcode is a switch in `BSByteCodeGenerator::write_for` and a block of handlers here,
// with no other file involved; the opcode numbers are already reserved for it (#243).
//
// `OPCODE_ITERATE_BEGIN`/`OPCODE_ITERATE` (any Variant) and `OPCODE_ITERATE_BEGIN_RANGE`/
// `OPCODE_ITERATE_RANGE` (the integer range, which has no container at all) are the four the
// emitter selects, and they are the four implemented below.

case OPCODE_ITERATE_BEGIN: {
	BS_CHECK_SPACE(5);
	BS_GET_VARIANT_PTR(counter, 0);
	BS_GET_VARIANT_PTR(container, 1);
	*counter = Variant();
	bool valid = false;
	if (!container->iter_init(*counter, valid)) {
		if (unlikely(!valid)) {
			error_text = vformat(R"(Cannot iterate over a value of type "%s".)",
					Variant::get_type_name(container->get_type()));
			goto vm_error;
		}
		const int target = code_ptr[BS_IP + 4];
		if (unlikely(target < 0 || target > code_size)) {
			error_text = "Bad loop exit target in compiled function.";
			goto vm_error;
		}
		BS_IP = target;
	} else {
		BS_GET_VARIANT_PTR(iterator, 2);
		*iterator = container->iter_get(*counter, valid);
		if (unlikely(!valid)) {
			error_text = vformat(R"(Cannot read the first element of a value of type "%s".)",
					Variant::get_type_name(container->get_type()));
			goto vm_error;
		}
		BS_IP += 5;
	}
} break;

case OPCODE_ITERATE: {
	BS_CHECK_SPACE(5);
	BS_GET_VARIANT_PTR(counter, 0);
	BS_GET_VARIANT_PTR(container, 1);
	bool valid = false;
	if (!container->iter_next(*counter, valid)) {
		if (unlikely(!valid)) {
			error_text = vformat(R"(Cannot advance an iteration over a value of type "%s".)",
					Variant::get_type_name(container->get_type()));
			goto vm_error;
		}
		const int target = code_ptr[BS_IP + 4];
		if (unlikely(target < 0 || target > code_size)) {
			error_text = "Bad loop exit target in compiled function.";
			goto vm_error;
		}
		BS_IP = target;
	} else {
		BS_GET_VARIANT_PTR(iterator, 2);
		*iterator = container->iter_get(*counter, valid);
		if (unlikely(!valid)) {
			error_text = vformat(R"(Cannot read the next element of a value of type "%s".)",
					Variant::get_type_name(container->get_type()));
			goto vm_error;
		}
		BS_IP += 5;
	}
} break;

case OPCODE_ITERATE_BEGIN_RANGE: {
	BS_CHECK_SPACE(7);
	BS_GET_VARIANT_PTR(counter, 0);
	BS_GET_VARIANT_PTR(from_value, 1);
	BS_GET_VARIANT_PTR(to_value, 2);
	BS_GET_VARIANT_PTR(step_value, 3);
	if (unlikely(from_value->get_type() != Variant::INT || to_value->get_type() != Variant::INT ||
				step_value->get_type() != Variant::INT)) {
		// The integer range is a distinct loop form, not a coercion: silently truncating 2.5 would
		// run a different loop than the one that was written.
		error_text = vformat(R"(A range loop takes integer bounds, not "%s", "%s" and "%s".)",
				Variant::get_type_name(from_value->get_type()), Variant::get_type_name(to_value->get_type()),
				Variant::get_type_name(step_value->get_type()));
		goto vm_error;
	}
	const int64_t from = *from_value;
	const int64_t to = *to_value;
	const int64_t step = *step_value;
	if (unlikely(step == 0)) {
		error_text = "A range loop's step cannot be zero.";
		goto vm_error;
	}
	*counter = from;
	const bool has_first = from == to ? false : (from < to ? step > 0 : step < 0);
	if (has_first) {
		BS_GET_VARIANT_PTR(iterator, 4);
		*iterator = from;
		BS_IP += 7;
	} else {
		const int target = code_ptr[BS_IP + 6];
		if (unlikely(target < 0 || target > code_size)) {
			error_text = "Bad loop exit target in compiled function.";
			goto vm_error;
		}
		BS_IP = target;
	}
} break;

case OPCODE_ITERATE_RANGE: {
	BS_CHECK_SPACE(6);
	BS_GET_VARIANT_PTR(counter, 0);
	BS_GET_VARIANT_PTR(to_value, 1);
	BS_GET_VARIANT_PTR(step_value, 2);
	const int64_t to = *to_value;
	const int64_t step = *step_value;
	// Advanced in unsigned arithmetic, which wraps to the same two's-complement value a signed
	// addition would produce but is defined when it does. The step past the last element is the one
	// that can leave the integer range -- `for x in range(0, 9223372036854775807, 9223372036854775807)`
	// takes it -- and it is computed before the comparison that stops the loop.
	const int64_t current = *counter;
	const int64_t next = (int64_t)((uint64_t)current + (uint64_t)step);
	*counter = next;
	// A wrapped advance moved the counter the wrong way, so the ordinary bound test would read it as
	// still inside the range and the loop would never end. Ending here is also what the range means:
	// there is no further integer in the direction the step points.
	const bool wrapped = step > 0 ? next < current : next > current;
	if (wrapped || (step < 0 && next <= to) || (step > 0 && next >= to)) {
		const int target = code_ptr[BS_IP + 5];
		if (unlikely(target < 0 || target > code_size)) {
			error_text = "Bad loop exit target in compiled function.";
			goto vm_error;
		}
		BS_IP = target;
	} else {
		BS_GET_VARIANT_PTR(iterator, 3);
		*iterator = next;
		BS_IP += 6;
	}
} break;
