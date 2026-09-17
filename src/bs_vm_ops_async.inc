/**************************************************************************/
/*  bs_vm_ops_async.inc                                                   */
/*                                                                        */
/*  Suspension handlers: await, resumption, and the callable-creating     */
/*  opcodes a lambda needs. Included inside the dispatch switch in        */
/*  bs_vm.cpp, which owns the operand-reading macros and the exit labels. */
/*                                                                        */
/*  Empty while suspension is unimplemented: an opcode with no handler    */
/*  here reaches the switch's default and raises an error naming itself,  */
/*  so nothing runs half a coroutine.                                     */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/
