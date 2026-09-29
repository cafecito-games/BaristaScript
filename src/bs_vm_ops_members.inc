/**************************************************************************/
/*  bs_vm_ops_members.inc                                                 */
/*                                                                        */
/*  Owner-object member access. Included inside the dispatch switch in    */
/*  bs_vm.cpp, which owns the operand-reading macros and the exit labels. */
/*                                                                        */
/*  These are the members of the object the script drives, not the        */
/*  script's own compiled members: those live in the instance's member    */
/*  array and are reached through an ordinary member address.             */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

case OPCODE_SET_MEMBER: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(source, 0);
	BS_GET_NAME(property, code_ptr[BS_IP + 2]);
	if (unlikely(p_instance == nullptr || p_instance->owner == nullptr)) {
		error_text = vformat(R"(Cannot set the property "%s": the frame has no instance.)", String(property));
		goto vm_error;
	}
	p_instance->owner->set(property, *source);
	BS_IP += 3;
} break;

case OPCODE_GET_MEMBER: {
	BS_CHECK_SPACE(3);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_NAME(property, code_ptr[BS_IP + 2]);
	if (unlikely(p_instance == nullptr || p_instance->owner == nullptr)) {
		error_text = vformat(R"(Cannot read the property "%s": the frame has no instance.)", String(property));
		goto vm_error;
	}
	*destination = p_instance->owner->get(property);
	BS_IP += 3;
} break;

case OPCODE_SET_STATIC_VARIABLE: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(value, 0);
	BS_GET_VARIANT_PTR(class_handle, 1);
	BaristaScript *class_script = script_from_handle(*class_handle);
	const int index = code_ptr[BS_IP + 3];
	if (unlikely(class_script == nullptr)) {
		error_text = "Cannot write a static variable: its class is no longer alive.";
		goto vm_error;
	}
	String static_error;
	if (unlikely(!class_script->set_static_value(index, *value, static_error))) {
		error_text = static_error.is_empty() ? String("Cannot write an unknown static variable.") : static_error;
		goto vm_error;
	}
	BS_IP += 4;
} break;

case OPCODE_GET_STATIC_VARIABLE: {
	BS_CHECK_SPACE(4);
	BS_GET_VARIANT_PTR(destination, 0);
	BS_GET_VARIANT_PTR(class_handle, 1);
	BaristaScript *class_script = script_from_handle(*class_handle);
	const int index = code_ptr[BS_IP + 3];
	if (unlikely(class_script == nullptr)) {
		error_text = "Cannot read a static variable: its class is no longer alive.";
		goto vm_error;
	}
	if (unlikely(!class_script->get_static_value(index, *destination))) {
		error_text = "Cannot read an unknown static variable.";
		goto vm_error;
	}
	BS_IP += 4;
} break;
