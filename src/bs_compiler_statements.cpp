/**************************************************************************/
/*  bs_compiler_statements.cpp                                            */
/*                                                                        */
/*  Statement lowering. Provenance: fs_compiler.cpp `_parse_block` and    */
/*  `_parse_statement` @ c9d5e35.                                         */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "barista_script.h"
#include "bs_compiler.h"

namespace barista_script {

namespace {

/**
 * True when a slot can hold a reference the frame would otherwise keep alive.
 *
 * Only these are worth clearing when a block ends: an `int` slot holds nothing anyone can observe
 * after the block, while an object, an array or an untyped slot pins whatever it last held for the
 * rest of the call. Provenance: fs_function.h:383-404 `can_contain_object` @ c9d5e35, minus the
 * element-type recursion, which needs the typed-container descriptor (#246); an untyped container
 * is treated as reference-bearing, which is the conservative side.
 */
bool slot_can_hold_a_reference(const BSParser::DataType &p_type) {
	if (p_type.kind != BSParser::DataType::BUILTIN) {
		return true;
	}
	switch (p_type.builtin_type) {
		case Variant::NIL:
		case Variant::OBJECT:
		case Variant::ARRAY:
		case Variant::DICTIONARY:
		case Variant::CALLABLE:
		case Variant::SIGNAL:
			return true;
		default:
			return false;
	}
}

/** The boolean slot type every pattern test writes into. */
BSParser::DataType boolean_slot_type() {
	BSParser::DataType type;
	type.kind = BSParser::DataType::BUILTIN;
	type.builtin_type = Variant::BOOL;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	return type;
}

} // namespace

bool BSCompiler::add_block_locals(CodeGen &p_codegen, const BSParser::SuiteNode *p_block, List<BSCodeGenerator::Address> &r_locals) {
	for (const BSParser::SuiteNode::Local &local : p_block->locals) {
		// A parameter is added by the function prologue and a loop variable is declared with the
		// loop's own scope, so neither is a block local here.
		if (local.type != BSParser::SuiteNode::Local::VARIABLE &&
				local.type != BSParser::SuiteNode::Local::PATTERN_BIND) {
			continue;
		}
		const BSParser::DataType slot = member_slot_type(local.get_datatype());
		if (local.type == BSParser::SuiteNode::Local::VARIABLE &&
				refuse_unchecked_slot(slot, vformat(R"(the local "%s")", String(local.name)), local.variable)) {
			return false;
		}
		r_locals.push_back(p_codegen.add_local(local.name, slot));
	}
	return true;
}

void BSCompiler::clear_block_locals(CodeGen &p_codegen, const List<BSCodeGenerator::Address> &p_locals) {
	for (const BSCodeGenerator::Address &local : p_locals) {
		if (slot_can_hold_a_reference(local.type)) {
			p_codegen.generator->clear_address(local);
		}
	}
}

void BSCompiler::clear_locals_left_by_jump(CodeGen &p_codegen) {
	if (loop_body_depths.is_empty()) {
		// A `break` or `continue` outside a loop is an analyzer error, and no scope is being left.
		return;
	}
	// Includes the loop body's own level. `continue` in a `while` lands on the condition, which is
	// evaluated before anything else clears the body, so an object the iteration held would still be
	// referenced while the next condition runs.
	const int body_depth = loop_body_depths.back()->get();
	int depth = 0;
	for (const List<BSCodeGenerator::Address> &scope : open_block_locals) {
		if (depth++ >= body_depth) {
			clear_block_locals(p_codegen, scope);
		}
	}
}

Error BSCompiler::parse_block(CodeGen &p_codegen, const BSParser::SuiteNode *p_block, bool p_add_locals) {
	BSCodeGenerator *generator = p_codegen.generator;
	p_codegen.start_block();

	List<BSCodeGenerator::Address> block_locals;
	if (p_add_locals && !add_block_locals(p_codegen, p_block, block_locals)) {
		return ERR_COMPILATION_FAILED;
	}
	// Registered before the statements run, because a `break` or a `continue` among them has to
	// clear this scope on its way out: the clear below sits after the last statement, which a jump
	// never reaches.
	const BlockScope scope(this, block_locals);

	for (const BSParser::Node *statement : p_block->statements) {
		generator->write_newline(statement->start_line);
		const Error result = parse_statement(p_codegen, p_block, statement);
		if (result != OK) {
			return result;
		}
		generator->clear_temporaries();
	}

	// A block that owns its locals also gives them up here, so nothing the block referenced outlives
	// it. A caller that allocated the locals itself -- a loop, which has to clear them once per
	// iteration and once on the way out -- passes `p_add_locals` false and does its own clearing.
	clear_block_locals(p_codegen, block_locals);

	p_codegen.end_block();
	return OK;
}

Error BSCompiler::parse_statement(CodeGen &p_codegen, const BSParser::SuiteNode *p_block, const BSParser::Node *p_statement) {
	BSCodeGenerator *generator = p_codegen.generator;
	Error result = OK;

	switch (p_statement->type) {
		case BSParser::Node::IF: {
			const BSParser::IfNode *node = static_cast<const BSParser::IfNode *>(p_statement);
			const BSCodeGenerator::Address condition = parse_expression(p_codegen, result, node->condition);
			if (result != OK) {
				return result;
			}
			generator->write_if(condition);
			if (condition.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			result = parse_block(p_codegen, node->true_block);
			if (result != OK) {
				return result;
			}
			if (node->false_block != nullptr) {
				generator->write_else();
				result = parse_block(p_codegen, node->false_block);
				if (result != OK) {
					return result;
				}
			}
			generator->write_endif();
		} break;

		case BSParser::Node::WHILE: {
			const BSParser::WhileNode *node = static_cast<const BSParser::WhileNode *>(p_statement);
			// The body's locals belong to a scope of their own, outside the body, so the loop can
			// clear them once per iteration (for `continue`) and once after the loop (for `break` and
			// for falling out), which a block that owns its own locals cannot do.
			p_codegen.start_block();
			generator->start_while_condition();
			const BSCodeGenerator::Address condition = parse_expression(p_codegen, result, node->condition);
			if (result != OK) {
				return result;
			}
			generator->write_while(condition);
			if (condition.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			List<BSCodeGenerator::Address> loop_locals;
			if (!add_block_locals(p_codegen, node->loop, loop_locals)) {
				return ERR_COMPILATION_FAILED;
			}
			{
				// The body's locals are a scope every exit from the body has to clear: falling off the
				// end (below), `continue` and `break` (through the jump cleanup, which is why the
				// marker names this scope), and the loop's own end (after `write_endwhile`). Clearing
				// at the *end* of the body rather than at its start is what keeps the next condition
				// evaluation from observing the previous iteration's values.
				const BlockScope body_scope(this, loop_locals);
				loop_body_depths.push_back(open_block_locals.size() - 1);
				result = parse_block(p_codegen, node->loop, false);
				loop_body_depths.pop_back();
				if (result != OK) {
					return result;
				}
				clear_block_locals(p_codegen, loop_locals);
			}
			generator->write_endwhile();
			clear_block_locals(p_codegen, loop_locals);
			p_codegen.end_block();
		} break;

		case BSParser::Node::FOR: {
			const BSParser::ForNode *node = static_cast<const BSParser::ForNode *>(p_statement);
			// The iteration variable and the loop's bookkeeping slots belong to a scope of their own,
			// outside the body, so the body can be re-entered without reallocating them.
			p_codegen.start_block();
			if (refuse_unchecked_slot(member_slot_type(node->variable->get_datatype()),
						vformat(R"(the loop variable "%s")", String(node->variable->name)), node->variable)) {
				return ERR_COMPILATION_FAILED;
			}
			const BSCodeGenerator::Address iterator = p_codegen.add_local(node->variable->name,
					member_slot_type(node->variable->get_datatype()));

			// `for i in range(...)` iterates an integer range directly instead of materializing an
			// array. The shortcut belongs to the engine's `range`, so a class that declares its own
			// keeps the ordinary call: the analyzer's classification of the callee is what decides.
			const BSParser::CallNode *range_call = nullptr;
			if (node->list != nullptr && node->list->type == BSParser::Node::CALL) {
				const BSParser::CallNode *call = static_cast<const BSParser::CallNode *>(node->list);
				if (call->get_callee_type() == BSParser::Node::IDENTIFIER) {
					const BSParser::IdentifierNode *callee = static_cast<const BSParser::IdentifierNode *>(call->callee);
					if (callee->name == StringName("range") &&
							callee->source == BSParser::IdentifierNode::UNDEFINED_SOURCE) {
						range_call = call;
					}
				}
			}

			generator->start_for(iterator.type, member_slot_type(node->list->get_datatype()), range_call != nullptr);

			if (range_call != nullptr) {
				Vector<BSCodeGenerator::Address> arguments;
				arguments.resize(range_call->arguments.size());
				for (int i = 0; i < arguments.size(); i++) {
					arguments.write[i] = parse_expression(p_codegen, result, range_call->arguments[i]);
					if (result != OK) {
						return result;
					}
				}
				switch (arguments.size()) {
					case 1:
						generator->write_for_range_assignment(p_codegen.add_constant(0), arguments[0], p_codegen.add_constant(1));
						break;
					case 2:
						generator->write_for_range_assignment(arguments[0], arguments[1], p_codegen.add_constant(1));
						break;
					case 3:
						generator->write_for_range_assignment(arguments[0], arguments[1], arguments[2]);
						break;
					default:
						set_error(R"*(A "range()" loop takes one, two or three arguments.)*", range_call);
						return ERR_COMPILATION_FAILED;
				}
				for (int i = arguments.size() - 1; i >= 0; i--) {
					if (arguments[i].mode == BSCodeGenerator::Address::TEMPORARY) {
						generator->pop_temporary();
					}
				}
			} else {
				const BSCodeGenerator::Address list = parse_expression(p_codegen, result, node->list);
				if (result != OK) {
					return result;
				}
				generator->write_for_list_assignment(list);
				if (list.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}

			generator->write_for(iterator, node->use_conversion_assign, range_call != nullptr);
			// Cleared inside the loop before the body so `continue` cannot carry an iteration's
			// locals into the next one, and again after the loop so `break` and normal exit do not
			// leave the last iteration's references pinned in the frame.
			List<BSCodeGenerator::Address> loop_locals;
			if (!add_block_locals(p_codegen, node->loop, loop_locals)) {
				return ERR_COMPILATION_FAILED;
			}
			{
				const BlockScope body_scope(this, loop_locals);
				loop_body_depths.push_back(open_block_locals.size() - 1);
				result = parse_block(p_codegen, node->loop, false);
				loop_body_depths.pop_back();
				if (result != OK) {
					return result;
				}
				clear_block_locals(p_codegen, loop_locals);
			}
			generator->write_endfor(range_call != nullptr);
			clear_block_locals(p_codegen, loop_locals);
			p_codegen.end_block();
		} break;

		case BSParser::Node::MATCH:
			return parse_match(p_codegen, static_cast<const BSParser::MatchNode *>(p_statement));

		case BSParser::Node::BREAK:
			clear_locals_left_by_jump(p_codegen);
			generator->write_break();
			break;

		case BSParser::Node::CONTINUE:
			clear_locals_left_by_jump(p_codegen);
			generator->write_continue();
			break;

		case BSParser::Node::PASS:
			break;

		case BSParser::Node::BREAKPOINT:
			generator->write_breakpoint();
			break;

		case BSParser::Node::RETURN: {
			const BSParser::ReturnNode *node = static_cast<const BSParser::ReturnNode *>(p_statement);
			if (node->return_value == nullptr || node->void_return) {
				if (node->return_value != nullptr) {
					const BSCodeGenerator::Address discarded = parse_expression(p_codegen, result, node->return_value);
					if (result != OK) {
						return result;
					}
					if (discarded.mode == BSCodeGenerator::Address::TEMPORARY) {
						generator->pop_temporary();
					}
				}
				generator->write_return(p_codegen.add_constant(Variant()));
			} else {
				const BSCodeGenerator::Address value = parse_expression(p_codegen, result, node->return_value);
				if (result != OK) {
					return result;
				}
				generator->write_return(value);
				if (value.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}
		} break;

		case BSParser::Node::ASSERT: {
			const BSParser::AssertNode *node = static_cast<const BSParser::AssertNode *>(p_statement);
			const BSCodeGenerator::Address condition = parse_expression(p_codegen, result, node->condition);
			if (result != OK) {
				return result;
			}
			BSCodeGenerator::Address message;
			if (node->message != nullptr) {
				message = parse_expression(p_codegen, result, node->message);
				if (result != OK) {
					return result;
				}
			}
			generator->write_assert(condition, message);
			if (message.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			if (condition.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
		} break;

		case BSParser::Node::VARIABLE: {
			const BSParser::VariableNode *node = static_cast<const BSParser::VariableNode *>(p_statement);
			if (!p_codegen.locals.has(node->identifier->name)) {
				p_codegen.add_local(node->identifier->name, member_slot_type(node->get_datatype()));
			}
			const BSCodeGenerator::Address local = p_codegen.locals[node->identifier->name];
			if (node->initializer != nullptr) {
				const BSCodeGenerator::Address value = parse_expression(p_codegen, result, node->initializer);
				if (result != OK) {
					return result;
				}
				if (node->use_conversion_assign) {
					generator->write_assign_with_conversion(local, value);
				} else {
					generator->write_assign(local, value);
				}
				if (value.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			} else if (local.type.kind == BSParser::DataType::BUILTIN || generator->is_local_dirty(local) || p_block->is_in_loop) {
				// A slot with no initializer must still hold a value of its own carrier: a builtin
				// cannot be null, and a reused slot would otherwise keep the previous iteration's value.
				generator->clear_address(local);
			}
		} break;

		case BSParser::Node::CONSTANT: {
			const BSParser::ConstantNode *node = static_cast<const BSParser::ConstantNode *>(p_statement);
			if (node->initializer == nullptr || !node->initializer->is_constant) {
				set_error("A local constant needs a constant initializer.", p_statement);
				return ERR_COMPILATION_FAILED;
			}
			p_codegen.add_local_constant(node->identifier->name, node->initializer->reduced_value);
		} break;

		default: {
			if (!p_statement->is_expression()) {
				set_error(vformat("The runtime cannot compile a %s statement.",
								  BSParser::get_node_type_name(p_statement->type)),
						p_statement);
				return ERR_COMPILATION_FAILED;
			}
			const BSCodeGenerator::Address value = parse_expression(p_codegen, result,
					static_cast<const BSParser::ExpressionNode *>(p_statement), true);
			if (result != OK) {
				return result;
			}
			if (value.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
		} break;
	}
	return OK;
}

Error BSCompiler::parse_match(CodeGen &p_codegen, const BSParser::MatchNode *p_match) {
	BSCodeGenerator *generator = p_codegen.generator;
	Error result = OK;

	// The subject and its `typeof` are evaluated exactly once, into locals of a scope that wraps the
	// whole statement. Re-reading the subject expression per branch would run its side effects again
	// and could even see a different value; every pattern therefore tests these two slots.
	p_codegen.start_block();

	const BSCodeGenerator::Address value = p_codegen.add_local(StringName("@match_value"),
			member_slot_type(p_match->test->get_datatype()));
	const BSCodeGenerator::Address value_expression = parse_expression(p_codegen, result, p_match->test);
	if (result != OK) {
		return result;
	}
	generator->write_assign(value, value_expression);
	if (value_expression.mode == BSCodeGenerator::Address::TEMPORARY) {
		generator->pop_temporary();
	}

	BSParser::DataType type_slot;
	type_slot.kind = BSParser::DataType::BUILTIN;
	type_slot.builtin_type = Variant::INT;
	type_slot.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	const BSCodeGenerator::Address subject_type = p_codegen.add_local(StringName("@match_type"), type_slot);
	Vector<BSCodeGenerator::Address> typeof_arguments;
	typeof_arguments.push_back(value);
	generator->write_call_utility(subject_type, StringName("typeof"), typeof_arguments);

	// The saved subject is a scope of its own as far as a jump is concerned: a `break` inside a
	// branch body leaves the `match` and must give the subject up on the way, exactly as the fall-out
	// below does.
	List<BSCodeGenerator::Address> subject_scope;
	subject_scope.push_back(value);
	const BlockScope match_scope(this, subject_scope);

	// Every slot any branch declares that can hold a reference, deduplicated by slot.
	//
	// Two properties of this list are load-bearing. It is filtered *before* deduplication, because
	// sibling branches reuse the same stack slots with different declared types: if branch A's body
	// declares an `int` in slot 7 and branch B binds an object into the same slot 7, keeping only
	// the first address seen would leave the clear looking at the `int` and deciding the slot holds
	// nothing. Any branch that can put a reference there is enough to make the slot worth clearing.
	// And it holds plain stack indices, which stay meaningful after a branch's scope ends: a slot
	// index is not looked up in any table, and the frame is sized by the high-water mark, so writing
	// into a released slot before any later block takes it is well defined.
	List<BSCodeGenerator::Address> bound_slots;
	const auto record_bound_slots = [&bound_slots](const List<BSCodeGenerator::Address> &p_locals) {
		for (const BSCodeGenerator::Address &local : p_locals) {
			if (!slot_can_hold_a_reference(local.type)) {
				continue;
			}
			bool already_recorded = false;
			for (const BSCodeGenerator::Address &known : bound_slots) {
				if (known.mode == local.mode && known.address == local.address) {
					already_recorded = true;
					break;
				}
			}
			if (!already_recorded) {
				bound_slots.push_back(local);
			}
		}
	};

	for (int index = 0; index < p_match->branches.size(); index++) {
		if (index > 0) {
			// Each branch is the `else` of the one before it, so a value that matched an earlier
			// pattern never reaches a later one.
			generator->write_else();
		}
		const BSParser::MatchBranchNode *branch = p_match->branches[index];

		// A branch's binds are locals of the branch, and they are allocated before the patterns run
		// so a pattern's temporaries cannot take the slot a bind is about to be written into.
		p_codegen.start_block();
		List<BSCodeGenerator::Address> branch_locals;
		if (!add_block_locals(p_codegen, branch->block, branch_locals)) {
			return ERR_COMPILATION_FAILED;
		}
		// A branch's binds are a scope a `break` or `continue` in its body leaves behind, like any
		// other block's locals, so the jump has to know about them too.
		const BlockScope branch_scope(this, branch_locals);
		record_bound_slots(branch_locals);
		generator->write_newline(branch->start_line);

		BSCodeGenerator::Address pattern_result = p_codegen.add_temporary(boolean_slot_type());
		for (int pattern = 0; pattern < branch->patterns.size(); pattern++) {
			pattern_result = parse_match_pattern(p_codegen, result, branch->patterns[pattern], value, subject_type,
					pattern_result, pattern == 0, false);
			if (result != OK) {
				return result;
			}
		}

		if (branch->guard_body != nullptr) {
			// The guard is AND-ed onto the pattern so it cannot run -- and cannot read a bind the
			// pattern did not write -- unless the pattern matched.
			generator->write_and_left_operand(pattern_result);
			if (branch->guard_body->statements.is_empty() ||
					!branch->guard_body->statements[0]->is_expression()) {
				set_error("A match guard needs a condition expression.", branch);
				return ERR_COMPILATION_FAILED;
			}
			const BSCodeGenerator::Address guard = parse_expression(p_codegen, result,
					static_cast<const BSParser::ExpressionNode *>(branch->guard_body->statements[0]));
			if (result != OK) {
				return result;
			}
			generator->write_and_right_operand(guard);
			generator->write_end_and(pattern_result);
			if (guard.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
		}

		generator->write_if(pattern_result);
		generator->pop_temporary();

		result = parse_block(p_codegen, branch->block, false);
		if (result != OK) {
			return result;
		}
		clear_block_locals(p_codegen, branch_locals);
		p_codegen.end_block();
	}

	for (int index = 0; index < p_match->branches.size(); index++) {
		generator->write_endif();
	}

	// A `match` with no arm matching and no wildcard falls straight through the last `else`: every
	// branch is a conditional, none of them is a default, so the statement is simply a no-op. That
	// is the documented behaviour (docs/GRAMMAR.md), and it is why no error is raised here.

	// A pattern writes its binds while it is being evaluated, before the branch is known to match: a
	// later alternative, a later element, or a guard can still turn the result false, and then the
	// branch body -- which is where that branch clears its own binds -- never runs. The bind slot
	// would keep whatever the failed attempt put in it for the rest of the frame. Clearing every
	// bound slot once, here, is the one place that is reached however the branches turned out.
	clear_block_locals(p_codegen, bound_slots);

	// The saved subject is a local like any other, and it is the only one in the frame that holds
	// the matched value. Leaving it set would keep a `RefCounted` subject alive for the rest of the
	// call, visible through its reference count, so the statement gives it up on the way out. The
	// saved `typeof` is an integer and holds nothing.
	if (slot_can_hold_a_reference(value.type)) {
		generator->clear_address(value);
	}
	p_codegen.end_block();
	return OK;
}

BSCodeGenerator::Address BSCompiler::parse_match_pattern(CodeGen &p_codegen, Error &r_error, const BSParser::PatternNode *p_pattern,
		const BSCodeGenerator::Address &p_value_addr, const BSCodeGenerator::Address &p_type_addr,
		const BSCodeGenerator::Address &p_previous_test, bool p_is_first, bool p_is_nested) {
	BSCodeGenerator *generator = p_codegen.generator;

	// Every pattern kind joins the accumulator the same way, so the join is written once here and
	// once at each `return`: AND for a sub-pattern (a failed parent must not let a child read a
	// value that is not there), OR for an alternative of the same branch, plain assignment for the
	// first alternative, which is what seeds the accumulator.
	const auto open_join = [&]() {
		if (p_is_nested) {
			generator->write_and_left_operand(p_previous_test);
		} else if (!p_is_first) {
			generator->write_or_left_operand(p_previous_test);
		}
	};
	const auto close_join = [&](const BSCodeGenerator::Address &p_test) {
		if (p_is_nested) {
			generator->write_and_right_operand(p_test);
			generator->write_end_and(p_previous_test);
		} else if (!p_is_first) {
			generator->write_or_right_operand(p_test);
			generator->write_end_or(p_previous_test);
		} else {
			generator->write_assign(p_previous_test, p_test);
		}
	};

	switch (p_pattern->pattern_type) {
		case BSParser::PatternNode::PT_LITERAL: {
			open_join();
			const Variant::Type literal_type = p_pattern->literal->value.get_type();
			const BSCodeGenerator::Address literal_type_addr = p_codegen.add_constant(literal_type);
			const BSCodeGenerator::Address type_equality = p_codegen.add_temporary(boolean_slot_type());
			generator->write_binary_operator(type_equality, Variant::OP_EQUAL, p_type_addr, literal_type_addr);

			// A String and a StringName carrying the same characters are the same value to the
			// language, so a literal of either spelling accepts a subject of the other.
			if (literal_type == Variant::STRING || literal_type == Variant::STRING_NAME) {
				const BSCodeGenerator::Address sibling = p_codegen.add_constant(
						literal_type == Variant::STRING ? Variant::STRING_NAME : Variant::STRING);
				const BSCodeGenerator::Address sibling_equality = p_codegen.add_temporary(boolean_slot_type());
				generator->write_binary_operator(sibling_equality, Variant::OP_EQUAL, p_type_addr, sibling);
				generator->write_binary_operator(type_equality, Variant::OP_OR, type_equality, sibling_equality);
				generator->pop_temporary();
			}

			generator->write_and_left_operand(type_equality);
			const BSCodeGenerator::Address literal = parse_expression(p_codegen, r_error, p_pattern->literal);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			const BSCodeGenerator::Address equality = p_codegen.add_temporary(boolean_slot_type());
			generator->write_binary_operator(equality, Variant::OP_EQUAL, p_value_addr, literal);
			generator->write_and_right_operand(equality);
			generator->write_end_and(type_equality);
			generator->pop_temporary();
			if (literal.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			close_join(type_equality);
			generator->pop_temporary();
			return p_previous_test;
		}

		case BSParser::PatternNode::PT_EXPRESSION: {
			open_join();
			if (p_pattern->is_subject_type_test) {
				// `match value: value is T:` is a type test against the saved subject, not a value
				// comparison, so the subject is never read a second time.
				const BSParser::TypeTestNode *test = static_cast<const BSParser::TypeTestNode *>(p_pattern->expression);
				if (!test->case_binds.is_empty()) {
					set_error("The runtime cannot compile a type test that binds a case payload.", p_pattern);
					r_error = ERR_COMPILATION_FAILED;
					return BSCodeGenerator::Address();
				}
				const BSCodeGenerator::Address test_result = p_codegen.add_temporary(boolean_slot_type());
				generator->write_type_test(test_result, p_value_addr, member_slot_type(test->test_datatype));
				if (generator->has_error()) {
					set_error(vformat("The runtime cannot compile %s.", generator->get_error()), p_pattern);
					r_error = ERR_COMPILATION_FAILED;
					return BSCodeGenerator::Address();
				}
				close_join(test_result);
				generator->pop_temporary();
				return p_previous_test;
			}

			const BSCodeGenerator::Address string_type = p_codegen.add_constant(Variant::STRING);
			const BSCodeGenerator::Address string_name_type = p_codegen.add_constant(Variant::STRING_NAME);
			// Taken result-first so the result outlives every operand, which is the temporary
			// discipline this file states at the top.
			const BSCodeGenerator::Address result_addr = p_codegen.add_temporary(boolean_slot_type());
			const BSCodeGenerator::Address equality_test = p_codegen.add_temporary(boolean_slot_type());
			const BSCodeGenerator::Address stringy = p_codegen.add_temporary(boolean_slot_type());
			const BSCodeGenerator::Address stringy_other = p_codegen.add_temporary(boolean_slot_type());
			const BSCodeGenerator::Address expression_type = p_codegen.add_temporary(BSParser::DataType());

			const BSCodeGenerator::Address expression = parse_expression(p_codegen, r_error, p_pattern->expression);
			if (r_error != OK) {
				return BSCodeGenerator::Address();
			}
			Vector<BSCodeGenerator::Address> typeof_arguments;
			typeof_arguments.push_back(expression);
			generator->write_call_utility(expression_type, StringName("typeof"), typeof_arguments);

			generator->write_binary_operator(result_addr, Variant::OP_EQUAL, p_type_addr, expression_type);
			// String against StringName, then StringName against String: the two spellings denote the
			// same value, so either pairing counts as a type match.
			generator->write_binary_operator(stringy, Variant::OP_EQUAL, p_type_addr, string_type);
			generator->write_binary_operator(stringy_other, Variant::OP_EQUAL, expression_type, string_name_type);
			generator->write_binary_operator(stringy, Variant::OP_AND, stringy, stringy_other);
			generator->write_binary_operator(result_addr, Variant::OP_OR, result_addr, stringy);
			generator->write_binary_operator(stringy, Variant::OP_EQUAL, p_type_addr, string_name_type);
			generator->write_binary_operator(stringy_other, Variant::OP_EQUAL, expression_type, string_type);
			generator->write_binary_operator(stringy, Variant::OP_AND, stringy, stringy_other);
			generator->write_binary_operator(result_addr, Variant::OP_OR, result_addr, stringy);

			generator->write_and_left_operand(result_addr);
			generator->write_binary_operator(equality_test, Variant::OP_EQUAL, p_value_addr, expression);
			generator->write_and_right_operand(equality_test);
			generator->write_end_and(result_addr);

			if (expression.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			generator->pop_temporary(); // expression_type
			generator->pop_temporary(); // stringy_other
			generator->pop_temporary(); // stringy
			generator->pop_temporary(); // equality_test
			close_join(result_addr);
			generator->pop_temporary(); // result_addr
			return p_previous_test;
		}

		case BSParser::PatternNode::PT_ARRAY: {
			open_join();
			const BSCodeGenerator::Address array_type = p_codegen.add_constant((int)Variant::ARRAY);
			const BSCodeGenerator::Address result_addr = p_codegen.add_temporary(boolean_slot_type());
			generator->write_binary_operator(result_addr, Variant::OP_EQUAL, p_type_addr, array_type);
			generator->write_and_left_operand(result_addr);

			const BSCodeGenerator::Address pattern_length = p_codegen.add_constant(
					p_pattern->rest_used ? p_pattern->array.size() - 1 : p_pattern->array.size());
			BSParser::DataType integer_slot;
			integer_slot.kind = BSParser::DataType::BUILTIN;
			integer_slot.builtin_type = Variant::INT;
			integer_slot.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			const BSCodeGenerator::Address value_length = p_codegen.add_temporary(integer_slot);
			Vector<BSCodeGenerator::Address> length_arguments;
			length_arguments.push_back(p_value_addr);
			generator->write_call_barista_script_utility(value_length, StringName("len"), length_arguments);
			const BSCodeGenerator::Address length_matches = p_codegen.add_temporary(boolean_slot_type());
			generator->write_binary_operator(length_matches,
					p_pattern->rest_used ? Variant::OP_GREATER_EQUAL : Variant::OP_EQUAL, value_length, pattern_length);
			generator->write_and_right_operand(length_matches);
			generator->write_end_and(result_addr);
			generator->pop_temporary(); // length_matches
			generator->pop_temporary(); // value_length

			const BSCodeGenerator::Address element = p_codegen.add_temporary(BSParser::DataType());
			const BSCodeGenerator::Address element_type = p_codegen.add_temporary(BSParser::DataType());
			BSCodeGenerator::Address accumulated = result_addr;
			for (int index = 0; index < p_pattern->array.size(); index++) {
				if (p_pattern->array[index]->pattern_type == BSParser::PatternNode::PT_REST) {
					// A rest pattern consumes the tail without naming it, so there is no element to read.
					break;
				}
				// AND-ed so a failed length or type test stops the walk before it indexes the value.
				generator->write_and_left_operand(accumulated);
				const BSCodeGenerator::Address index_addr = p_codegen.add_constant(index);
				generator->write_get(element, index_addr, p_value_addr);
				Vector<BSCodeGenerator::Address> typeof_arguments;
				typeof_arguments.push_back(element);
				generator->write_call_utility(element_type, StringName("typeof"), typeof_arguments);
				accumulated = parse_match_pattern(p_codegen, r_error, p_pattern->array[index], element, element_type,
						accumulated, false, true);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				generator->write_and_right_operand(accumulated);
				generator->write_end_and(accumulated);
			}
			generator->pop_temporary(); // element_type
			generator->pop_temporary(); // element
			close_join(accumulated);
			generator->pop_temporary(); // result_addr
			return p_previous_test;
		}

		case BSParser::PatternNode::PT_DICTIONARY: {
			open_join();
			const BSCodeGenerator::Address dictionary_type = p_codegen.add_constant((int)Variant::DICTIONARY);
			const BSCodeGenerator::Address result_addr = p_codegen.add_temporary(boolean_slot_type());
			generator->write_binary_operator(result_addr, Variant::OP_EQUAL, p_type_addr, dictionary_type);
			generator->write_and_left_operand(result_addr);

			const BSCodeGenerator::Address pattern_length = p_codegen.add_constant(
					p_pattern->rest_used ? p_pattern->dictionary.size() - 1 : p_pattern->dictionary.size());
			BSParser::DataType integer_slot;
			integer_slot.kind = BSParser::DataType::BUILTIN;
			integer_slot.builtin_type = Variant::INT;
			integer_slot.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			const BSCodeGenerator::Address value_length = p_codegen.add_temporary(integer_slot);
			Vector<BSCodeGenerator::Address> length_arguments;
			length_arguments.push_back(p_value_addr);
			generator->write_call_barista_script_utility(value_length, StringName("len"), length_arguments);
			const BSCodeGenerator::Address length_matches = p_codegen.add_temporary(boolean_slot_type());
			generator->write_binary_operator(length_matches,
					p_pattern->rest_used ? Variant::OP_GREATER_EQUAL : Variant::OP_EQUAL, value_length, pattern_length);
			generator->write_and_right_operand(length_matches);
			generator->write_end_and(result_addr);
			generator->pop_temporary(); // length_matches
			generator->pop_temporary(); // value_length

			const BSCodeGenerator::Address element = p_codegen.add_temporary(BSParser::DataType());
			const BSCodeGenerator::Address element_type = p_codegen.add_temporary(BSParser::DataType());
			BSCodeGenerator::Address accumulated = result_addr;
			for (const BSParser::PatternNode::Pair &entry : p_pattern->dictionary) {
				if (entry.value_pattern != nullptr && entry.value_pattern->pattern_type == BSParser::PatternNode::PT_REST) {
					break;
				}
				generator->write_and_left_operand(accumulated);
				const BSCodeGenerator::Address key = parse_expression(p_codegen, r_error, entry.key);
				if (r_error != OK) {
					return BSCodeGenerator::Address();
				}
				Vector<BSCodeGenerator::Address> has_arguments;
				has_arguments.push_back(key);
				generator->write_call(accumulated, p_value_addr, StringName("has"), has_arguments);
				if (entry.value_pattern != nullptr) {
					// The key's presence is proven before the value is read, so a missing key cannot
					// turn into an invalid access.
					generator->write_and_left_operand(accumulated);
					generator->write_get(element, key, p_value_addr);
					Vector<BSCodeGenerator::Address> typeof_arguments;
					typeof_arguments.push_back(element);
					generator->write_call_utility(element_type, StringName("typeof"), typeof_arguments);
					accumulated = parse_match_pattern(p_codegen, r_error, entry.value_pattern, element, element_type,
							accumulated, false, true);
					if (r_error != OK) {
						return BSCodeGenerator::Address();
					}
					generator->write_and_right_operand(accumulated);
					generator->write_end_and(accumulated);
				}
				generator->write_and_right_operand(accumulated);
				generator->write_end_and(accumulated);
				if (key.mode == BSCodeGenerator::Address::TEMPORARY) {
					generator->pop_temporary();
				}
			}
			generator->pop_temporary(); // element_type
			generator->pop_temporary(); // element
			close_join(accumulated);
			generator->pop_temporary(); // result_addr
			return p_previous_test;
		}

		case BSParser::PatternNode::PT_REST:
			// A rest pattern contributes no test of its own: the arity test the container pattern
			// already wrote is the whole of what it means.
			return p_previous_test;

		case BSParser::PatternNode::PT_BIND:
		case BSParser::PatternNode::PT_WILDCARD: {
			open_join();
			if (p_pattern->pattern_type == BSParser::PatternNode::PT_BIND) {
				// A bind matches anything and names what it matched. The write happens here, inside
				// the short-circuit the container pattern opened, so a bind nested under a failed test
				// is never written.
				if (!p_codegen.locals.has(p_pattern->bind->name)) {
					set_error(vformat(R"(The runtime has no slot for the pattern bind "%s".)", String(p_pattern->bind->name)),
							p_pattern);
					r_error = ERR_COMPILATION_FAILED;
					return BSCodeGenerator::Address();
				}
				generator->write_assign(p_codegen.locals[p_pattern->bind->name], p_value_addr);
			}
			if (p_is_nested || !p_is_first) {
				const BSCodeGenerator::Address always = p_codegen.add_constant(true);
				close_join(always);
			} else {
				generator->write_assign_true(p_previous_test);
			}
			return p_previous_test;
		}

		case BSParser::PatternNode::PT_TUPLE:
		case BSParser::PatternNode::PT_ENUM_CASE:
			break;
	}

	// A tuple or tagged-union case pattern needs the runtime type descriptor those families bring
	// (#246 and #247). Refusing by name keeps a half-matched value from ever reaching a branch body.
	set_error("The runtime cannot compile this match pattern.", p_pattern);
	r_error = ERR_COMPILATION_FAILED;
	return p_previous_test;
}

} // namespace barista_script
