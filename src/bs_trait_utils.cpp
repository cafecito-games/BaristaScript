/**************************************************************************/
/*  bs_trait_utils.cpp                                                    */
/*                                                                        */
/*  Hard fork of Foundry fs_trait_utils.cpp @ c9d5e35. Binding / Self     */
/*  reify / project_conformance_trait_arguments walkers for ClassTrait   */
/*  Binding publish (#60). class_has_named_trait remains residual.        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_trait_utils.h"

namespace barista_script {

namespace {

// Mirrors Variant::MAX_RECURSION_DEPTH (1024).
static constexpr int TYPE_WALK_MAX_DEPTH = 1024;

static bool _same_trait_declaration(const BSParser::ClassNode *p_a, const BSParser::ClassNode *p_b) {
	if (p_a == p_b) {
		return true;
	}
	return p_a != nullptr && p_b != nullptr && !p_a->fqcn.is_empty() && p_a->fqcn == p_b->fqcn;
}

} // namespace

Vector<BSParser::ClassNode *> bs_trait_identity_closure_nodes(const BSParser::ClassNode *p_trait) {
	Vector<BSParser::ClassNode *> nodes;
	if (p_trait == nullptr) {
		return nodes;
	}

	HashSet<StringName> seen;
	auto append_identity = [&](BSParser::ClassNode *p_member) {
		const StringName identity = bs_trait_identity_name(p_member);
		if (identity != StringName() && !seen.has(identity)) {
			seen.insert(identity);
			nodes.push_back(p_member);
		}
	};

	append_identity(const_cast<BSParser::ClassNode *>(p_trait));
	for (int i = 0; i < p_trait->resolved_traits.size(); i++) {
		BSParser::ClassNode *supertrait = p_trait->resolved_traits[i];
		if (supertrait != nullptr) {
			append_identity(supertrait);
		}
	}
	return nodes;
}

HashMap<StringName, BSParser::DataType> bs_trait_use_type_argument_bindings(
		const BSParser::ClassNode *p_trait, const BSParser::ClassNode::TraitUse &p_trait_use) {
	HashMap<StringName, BSParser::DataType> bindings;
	if (p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}

	const int count = MIN(p_trait->type_parameters.size(), p_trait_use.resolved_type_arguments.size());
	for (int i = 0; i < count; i++) {
		const BSParser::TypeParameterNode *type_parameter = p_trait->type_parameters[i];
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			bindings.insert(type_parameter->identifier->name, p_trait_use.resolved_type_arguments[i]);
		}
	}
	return bindings;
}

static HashMap<StringName, BSParser::DataType> _trait_type_argument_bindings(
		const BSParser::ClassNode *p_class, const BSParser::ClassNode *p_trait, int p_depth) {
	HashMap<StringName, BSParser::DataType> bindings;
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return bindings;
	}
	if (p_class == nullptr || p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}

	for (int i = 0; i < p_class->used_traits.size(); i++) {
		const BSParser::ClassNode::TraitUse &trait_use = p_class->used_traits[i];
		const BSParser::ClassNode *used_trait = trait_use.resolved_trait;
		if (used_trait == nullptr) {
			continue;
		}
		if (_same_trait_declaration(used_trait, p_trait)) {
			bindings = bs_trait_use_type_argument_bindings(p_trait, trait_use);
			if (bindings.is_empty()) {
				continue;
			}
			return bindings;
		}

		bool reaches_trait = false;
		for (int t = 0; t < used_trait->resolved_traits.size(); t++) {
			const BSParser::ClassNode *supertrait = used_trait->resolved_traits[t];
			if (_same_trait_declaration(supertrait, p_trait)) {
				reaches_trait = true;
				break;
			}
		}
		if (reaches_trait) {
			const HashMap<StringName, BSParser::DataType> inner =
					_trait_type_argument_bindings(used_trait, p_trait, p_depth + 1);
			if (inner.is_empty()) {
				continue;
			}
			const HashMap<StringName, BSParser::DataType> outer =
					_trait_type_argument_bindings(p_class, used_trait, p_depth + 1);
			for (const KeyValue<StringName, BSParser::DataType> &binding : inner) {
				bindings.insert(binding.key, BSParser::DataType::substitute(binding.value, outer));
			}
			return bindings;
		}
	}
	return bindings;
}

HashMap<StringName, BSParser::DataType> bs_trait_type_argument_bindings(
		const BSParser::ClassNode *p_class, const BSParser::ClassNode *p_trait) {
	return _trait_type_argument_bindings(p_class, p_trait, 0);
}

static bool _references_self(const BSParser::DataType &p_argument, int p_depth) {
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return true;
	}
	if (p_argument.kind == BSParser::DataType::TYPE_PARAMETER &&
			p_argument.type_parameter_name == SNAME("@Self")) {
		return true;
	}
	for (const BSParser::DataType &element : p_argument.container_element_types) {
		if (_references_self(element, p_depth + 1)) {
			return true;
		}
	}
	for (const BSParser::DataType &type_argument : p_argument.type_arguments) {
		if (_references_self(type_argument, p_depth + 1)) {
			return true;
		}
	}
	for (const BSParser::DataType &member : p_argument.union_members) {
		if (_references_self(member, p_depth + 1)) {
			return true;
		}
	}
	return false;
}

bool bs_trait_argument_references_self(const BSParser::DataType &p_argument) {
	return _references_self(p_argument, 0);
}

bool bs_trait_implementer_reifies_self(const BSParser::ClassNode *p_implementer) {
	return p_implementer != nullptr && !p_implementer->is_trait && p_implementer->is_final &&
			p_implementer->type_parameters.is_empty();
}

BSParser::DataType bs_reify_self_in_trait_argument(
		const BSParser::ClassNode *p_implementer, const BSParser::DataType &p_argument) {
	if (!bs_trait_implementer_reifies_self(p_implementer) || !bs_trait_argument_references_self(p_argument)) {
		return p_argument;
	}

	BSParser::DataType self_type = p_implementer->get_datatype();
	if (!self_type.is_set()) {
		return p_argument;
	}
	self_type.is_meta_type = false;
	self_type.is_pseudo_type = false;
	self_type.is_constant = false;
	self_type.type_arguments.clear();

	HashMap<StringName, BSParser::DataType> bindings;
	bindings.insert(SNAME("@Self"), self_type);
	return BSParser::DataType::substitute(p_argument, bindings);
}

bool bs_project_conformance_trait_arguments(
		const BSParser::ClassNode *p_direct_trait,
		const Vector<BSParser::DataType> &p_conformance_arguments,
		const HashMap<StringName, BSParser::DataType> &p_direct_bindings,
		const BSParser::ClassNode *p_identity_trait,
		const BSParser::ClassNode *p_implementer,
		Vector<BSParser::DataType> &r_arguments) {
	r_arguments.clear();
	if (p_direct_trait == nullptr || p_identity_trait == nullptr || p_identity_trait->type_parameters.is_empty()) {
		return false;
	}

	if (p_identity_trait == p_direct_trait) {
		if (p_conformance_arguments.size() != p_identity_trait->type_parameters.size()) {
			return false;
		}
		for (int i = 0; i < p_conformance_arguments.size(); i++) {
			r_arguments.push_back(bs_reify_self_in_trait_argument(p_implementer, p_conformance_arguments[i]));
		}
		return true;
	}

	const HashMap<StringName, BSParser::DataType> substitution =
			bs_trait_type_argument_bindings(p_direct_trait, p_identity_trait);
	Vector<BSParser::DataType> projected;
	for (int i = 0; i < p_identity_trait->type_parameters.size(); i++) {
		const BSParser::TypeParameterNode *type_parameter = p_identity_trait->type_parameters[i];
		if (type_parameter == nullptr || type_parameter->identifier == nullptr) {
			return false;
		}
		const BSParser::DataType *bound = substitution.getptr(type_parameter->identifier->name);
		if (bound == nullptr) {
			return false;
		}
		projected.push_back(bs_reify_self_in_trait_argument(
				p_implementer, BSParser::DataType::substitute(*bound, p_direct_bindings)));
	}
	r_arguments = projected;
	return true;
}

} // namespace barista_script
