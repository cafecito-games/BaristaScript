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
	const int64_t from = *from_value;
	const int64_t to = *to_value;
	const int64_t step = *step_value;
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
	const int64_t next = (int64_t)*counter + step;
	*counter = next;
	if ((step < 0 && next <= to) || (step > 0 && next >= to)) {
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
