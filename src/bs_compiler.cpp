/**************************************************************************/
/*  bs_compiler.cpp                                                       */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_compiler.h"

#include "barista_script.h"
#include "bs_byte_codegen.h"

namespace barista_script {

namespace {

BSDeclarationKind declaration_kind_of(const BSParser::ClassNode *p_class) {
	if (p_class->is_enum_file) {
		return BSDeclarationKind::ENUM;
	}
	if (p_class->is_tuple_file) {
		return BSDeclarationKind::TUPLE;
	}
	if (p_class->trait_name_used) {
		return BSDeclarationKind::TRAIT;
	}
	if (p_class->identifier != nullptr) {
		return p_class->type_parameters.is_empty() ? BSDeclarationKind::CLASS : BSDeclarationKind::GENERIC_CLASS;
	}
	return BSDeclarationKind::NONE;
}

} // namespace

void BSCompiler::set_error(const String &p_message, const BSParser::Node *p_origin) {
	if (!error.is_empty()) {
		return;
	}
	error = p_message;
	error_line = p_origin != nullptr ? p_origin->start_line : -1;
}

BaristaScript *BSCompiler::find_static_owner(const BSParser::VariableNode *p_variable, int &r_index) const {
	r_index = -1;
	if (p_variable == nullptr || p_variable->identifier == nullptr) {
		return nullptr;
	}
	for (const KeyValue<const BSParser::ClassNode *, Ref<BaristaScript>> &entry : class_scripts) {
		for (const BSParser::ClassNode::Member &member : entry.key->members) {
			if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable == p_variable) {
				r_index = entry.value->get_static_index(p_variable->identifier->name);
				return entry.value.ptr();
			}
		}
	}
	return nullptr;
}

Error BSCompiler::compile(const BSParser *p_parser, BaristaScript *p_script) {
	ERR_FAIL_NULL_V(p_parser, ERR_INVALID_PARAMETER);
	ERR_FAIL_NULL_V(p_script, ERR_INVALID_PARAMETER);
	error = String();
	error_line = -1;

	const BSParser::ClassNode *root = p_parser->get_tree();
	if (root == nullptr) {
		set_error("The script has no class to compile.", nullptr);
		return ERR_COMPILATION_FAILED;
	}
	p_script->declaration_kind_known = true;
	p_script->declaration_kind = declaration_kind_of(root);
	p_script->compiled_abstract = root->is_abstract;
	return compile_class_tree(p_script, root);
}

void BSCompiler::register_class_tree(const BSParser::ClassNode *p_class, const Ref<BaristaScript> &p_script) {
	class_scripts[p_class] = p_script;
	for (const BSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != BSParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
			continue;
		}
		Ref<BaristaScript> inner;
		inner.instantiate();
		inner->source_code = p_script->source_code;
		inner->set_name(member.m_class->identifier != nullptr ? member.m_class->identifier->name : StringName());
		inner->declaration_kind_known = true;
		inner->declaration_kind = BSDeclarationKind::CLASS;
		inner->compiled_abstract = member.m_class->is_abstract;
		register_class_tree(member.m_class, inner);
	}
}

Error BSCompiler::compile_registered_class(const BSParser::ClassNode *p_class) {
	if (compiled_classes.has(p_class)) {
		return OK;
	}
	if (compiling_classes.has(p_class)) {
		set_error("The runtime cannot compile a cyclic inner-class inheritance chain.", p_class);
		return ERR_COMPILATION_FAILED;
	}
	compiling_classes.insert(p_class);
	if (p_class->base_type.class_type != nullptr && class_scripts.has(p_class->base_type.class_type) &&
			compile_registered_class(p_class->base_type.class_type) != OK) {
		compiling_classes.erase(p_class);
		return ERR_COMPILATION_FAILED;
	}
	Ref<BaristaScript> *registered = class_scripts.getptr(p_class);
	if (registered == nullptr) {
		set_error("The runtime lost a registered inner class while compiling it.", p_class);
		compiling_classes.erase(p_class);
		return ERR_COMPILATION_FAILED;
	}
	Ref<BaristaScript> script = *registered;
	const Error result = compile_class(script.ptr(), p_class);
	compiling_classes.erase(p_class);
	if (result != OK) {
		return result;
	}
	compiled_classes.insert(p_class);
	return OK;
}

Error BSCompiler::compile_class_tree(BaristaScript *p_root_script, const BSParser::ClassNode *p_root) {
	class_scripts.clear();
	compiled_classes.clear();
	compiling_classes.clear();
	register_class_tree(p_root, Ref<BaristaScript>(p_root_script));
	for (const KeyValue<const BSParser::ClassNode *, Ref<BaristaScript>> &receiver : class_scripts) {
		receiver.value->compiled_class_ids.clear();
		for (const KeyValue<const BSParser::ClassNode *, Ref<BaristaScript>> &declared : class_scripts) {
			receiver.value->compiled_class_ids[declared.key->fqcn] = declared.value->get_instance_id();
		}
	}

	for (const KeyValue<const BSParser::ClassNode *, Ref<BaristaScript>> &entry : class_scripts) {
		if (compile_registered_class(entry.key) != OK) {
			return ERR_COMPILATION_FAILED;
		}
	}

	// Publish inner classes only after every class compiled successfully. The owning outer keeps the
	// class resource alive; base links remain weak so extracting a child cannot create an ownership
	// cycle through its outer class.
	for (const KeyValue<const BSParser::ClassNode *, Ref<BaristaScript>> &entry : class_scripts) {
		BaristaScript *owner = entry.value.ptr();
		for (const BSParser::ClassNode::Member &member : entry.key->members) {
			if (member.type != BSParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
					member.m_class->identifier == nullptr) {
				continue;
			}
			const StringName name = member.m_class->identifier->name;
			const Ref<BaristaScript> *inner_ptr = class_scripts.getptr(member.m_class);
			if (inner_ptr == nullptr) {
				set_error("The runtime lost a registered inner class while publishing it.", member.m_class);
				return ERR_COMPILATION_FAILED;
			}
			const Ref<BaristaScript> inner = *inner_ptr;
			owner->inner_classes[name] = inner;
			owner->constants[name] = inner;
		}
	}

	// Static initialization is deterministic and follows lexical class order among classes whose
	// inner-class base has already initialized. This keeps a forward-declared child from observing
	// its base before the base's static state exists.
	Vector<const BSParser::ClassNode *> pending;
	pending.push_back(p_root);
	for (int index = 0; index < pending.size(); index++) {
		const BSParser::ClassNode *node = pending[index];
		for (const BSParser::ClassNode::Member &member : node->members) {
			if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
				pending.push_back(member.m_class);
			}
		}
	}

	HashSet<const BSParser::ClassNode *> initialized;
	while (initialized.size() < pending.size()) {
		bool made_progress = false;
		for (const BSParser::ClassNode *node : pending) {
			if (initialized.has(node)) {
				continue;
			}
			const BSParser::ClassNode *base = node->base_type.class_type;
			if (base != nullptr && class_scripts.has(base) && !initialized.has(base)) {
				continue;
			}
			const Ref<BaristaScript> *script_ref = class_scripts.getptr(node);
			if (script_ref == nullptr) {
				set_error("The runtime lost a registered inner class while initializing it.", node);
				return ERR_COMPILATION_FAILED;
			}
			BaristaScript *script = script_ref->ptr();
			if (script->static_initializer != nullptr) {
				GDExtensionCallError call_error;
				script->static_initializer->call(nullptr, nullptr, 0, call_error, script);
				if (call_error.error != GDEXTENSION_CALL_OK || bs_runtime_error_was_reported()) {
					set_error(vformat(R"(The static initializer for "%s" did not run to completion.)",
									  node->identifier != nullptr ? String(node->identifier->name) : String("<script>")),
							node);
					return ERR_COMPILATION_FAILED;
				}
			}
			BSFunction *const *user_initializer_ptr = script->member_functions.getptr(SNAME("_static_init"));
			if (user_initializer_ptr != nullptr && *user_initializer_ptr != nullptr) {
				BSFunction *user_initializer = *user_initializer_ptr;
				GDExtensionCallError call_error;
				user_initializer->call(nullptr, nullptr, 0, call_error, script);
				if (call_error.error != GDEXTENSION_CALL_OK || bs_runtime_error_was_reported()) {
					set_error(vformat(R"(The static initializer for "%s" did not run to completion.)",
									  node->identifier != nullptr ? String(node->identifier->name) : String("<script>")),
							node);
					return ERR_COMPILATION_FAILED;
				}
			}
			initialized.insert(node);
			made_progress = true;
		}
		if (!made_progress) {
			set_error("The runtime cannot initialize a cyclic inner-class inheritance chain.", p_root);
			return ERR_COMPILATION_FAILED;
		}
	}
	return OK;
}

bool BSCompiler::is_local_or_parameter(const CodeGen &p_codegen, const StringName &p_name) {
	return p_codegen.parameters.has(p_name) || p_codegen.locals.has(p_name);
}

} // namespace barista_script
