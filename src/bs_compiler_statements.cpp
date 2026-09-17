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

Error BSCompiler::parse_block(CodeGen &p_codegen, const BSParser::SuiteNode *p_block, bool p_add_locals) {
	BSCodeGenerator *generator = p_codegen.generator;
	p_codegen.start_block();

	if (p_add_locals) {
		// A block's locals exist for the whole block: a later statement can name one a jump skipped
		// over, so the slots are allocated on entry rather than at each declaration.
		for (const BSParser::SuiteNode::Local &local : p_block->locals) {
			if (local.type == BSParser::SuiteNode::Local::VARIABLE) {
				p_codegen.add_local(local.name, member_slot_type(local.get_datatype()));
			}
		}
	}

	for (const BSParser::Node *statement : p_block->statements) {
		generator->write_newline(statement->start_line);
		const Error result = parse_statement(p_codegen, p_block, statement);
		if (result != OK) {
			return result;
		}
		generator->clear_temporaries();
	}

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
			generator->start_while_condition();
			const BSCodeGenerator::Address condition = parse_expression(p_codegen, result, node->condition);
			if (result != OK) {
				return result;
			}
			generator->write_while(condition);
			if (condition.mode == BSCodeGenerator::Address::TEMPORARY) {
				generator->pop_temporary();
			}
			result = parse_block(p_codegen, node->loop);
			if (result != OK) {
				return result;
			}
			generator->write_endwhile();
		} break;

		case BSParser::Node::FOR: {
			const BSParser::ForNode *node = static_cast<const BSParser::ForNode *>(p_statement);
			// The iteration variable and the loop's bookkeeping slots belong to a scope of their own,
			// outside the body, so the body can be re-entered without reallocating them.
			p_codegen.start_block();
			const BSCodeGenerator::Address iterator = p_codegen.add_local(node->variable->name,
					member_slot_type(node->variable->get_datatype()));

			// `for i in range(...)` iterates an integer range directly instead of materializing an array.
			const BSParser::CallNode *range_call = nullptr;
			if (node->list != nullptr && node->list->type == BSParser::Node::CALL) {
				const BSParser::CallNode *call = static_cast<const BSParser::CallNode *>(node->list);
				if (call->get_callee_type() == BSParser::Node::IDENTIFIER &&
						static_cast<const BSParser::IdentifierNode *>(call->callee)->name == StringName("range")) {
					range_call = call;
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
			result = parse_block(p_codegen, node->loop);
			if (result != OK) {
				return result;
			}
			generator->write_endfor(range_call != nullptr);
			p_codegen.end_block();
		} break;

		case BSParser::Node::BREAK:
			generator->write_break();
			break;

		case BSParser::Node::CONTINUE:
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

} // namespace barista_script
