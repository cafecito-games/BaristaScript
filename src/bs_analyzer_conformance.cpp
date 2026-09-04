/**************************************************************************/
/*  bs_analyzer_conformance.cpp                                           */
/*                                                                        */
/*  Hard fork of Foundry `modules/foundry_script/fs_analyzer_conformance   */
/*  .cpp` @ c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6 (plus               */
/*  validate_trait_requirements / find_trait_implementation from          */
/*  fs_analyzer.cpp). FS* -> BS*; engine contact through bs_platform.h.   */
/*  Starter slice for #60: trait abstract-method requirements,            */
/*  retroactive `extend` missing-witness diagnostics, and witness-body    */
/*  analysis hooks. Full FSConformanceRegistry / chain-coherence /        */
/*  signature substitution remain follow-up under #60.                    */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_analyzer.h"

#include "barista_script.h"
#include "barista_script_language.h"
#include "bs_cache.h"
#include "bs_native_db.h"
#include "bs_platform.h"
#include "bs_trait_utils.h"

namespace barista_script {

namespace {

const BSParser::Node *_trait_use_source(const BSParser::ClassNode::TraitUse &p_trait_use,
		const BSParser::ClassNode *p_owner) {
	if (!p_trait_use.name.is_empty()) {
		return p_trait_use.name[0];
	}
	return p_owner;
}

const BSParser::Node *_trait_requirement_source(const BSParser::ClassNode *p_class,
		BSParser::ClassNode *p_trait) {
	for (const BSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		if (trait_use.resolved_trait == p_trait) {
			return _trait_use_source(trait_use, p_class);
		}
		if (trait_use.resolved_trait != nullptr) {
			for (int i = 0; i < trait_use.resolved_trait->resolved_traits.size(); i++) {
				if (trait_use.resolved_trait->resolved_traits[i] == p_trait) {
					return _trait_use_source(trait_use, p_class);
				}
			}
		}
	}
	return p_class->identifier != nullptr ? static_cast<const BSParser::Node *>(p_class->identifier) : p_class;
}

BSParser::ClassNode *_find_local_class_by_name(BSParser::ClassNode *p_root, const StringName &p_name) {
	if (p_root == nullptr || p_name == StringName()) {
		return nullptr;
	}
	if (p_root->identifier != nullptr && p_root->identifier->name == p_name) {
		return p_root;
	}
	for (int i = 0; i < p_root->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_root->members[i];
		if (member.type != BSParser::ClassNode::Member::CLASS || member.m_class == nullptr) {
			continue;
		}
		BSParser::ClassNode *found = _find_local_class_by_name(member.m_class, p_name);
		if (found != nullptr) {
			return found;
		}
	}
	return nullptr;
}

BSParser::ClassNode *_find_local_trait_by_name(BSParser::ClassNode *p_owner, const String &p_name) {
	for (BSParser::ClassNode *scope = p_owner; scope != nullptr; scope = scope->outer) {
		if (!scope->has_member(StringName(p_name))) {
			continue;
		}
		const BSParser::ClassNode::Member member = scope->get_member(StringName(p_name));
		if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr && member.m_class->is_trait) {
			return member.m_class;
		}
	}
	return nullptr;
}

} // namespace

bool BSAnalyzer::find_trait_implementation(BSParser::ClassNode *p_class, const StringName &p_function_name,
		TraitMethodImplementation &r_implementation) {
	r_implementation = TraitMethodImplementation();
	BSParser::ClassNode *current_class = p_class;
	HashSet<BSParser::ClassNode *> visited_classes;
	while (current_class != nullptr) {
		if (visited_classes.has(current_class)) {
			break;
		}
		visited_classes.insert(current_class);

		if (current_class->is_builtin_conformance_shim) {
			// Builtin MethodInfo surface remains follow-up under #60 when godot-cpp exposes it.
			return false;
		}

		if (current_class->has_function(p_function_name)) {
			BSParser::FunctionNode *function = current_class->get_member(p_function_name).function;
			if (function != nullptr && !function->is_abstract) {
				r_implementation.function = function;
				r_implementation.owner_class = current_class;
				return true;
			}
		}

		if (current_class->base_type.kind == BSParser::DataType::CLASS && current_class->base_type.class_type != nullptr) {
			current_class = current_class->base_type.class_type;
			continue;
		}
		if (current_class->base_type.kind == BSParser::DataType::SCRIPT && !current_class->base_type.script_path.is_empty()) {
			Error err = OK;
			Ref<BSParserRef> base_ref = BSCache::get_parser(current_class->base_type.script_path, BSParserRef::INTERFACE_SOLVED, err,
					parser != nullptr ? parser->script_path : String());
			if (base_ref.is_valid() && err == OK && base_ref->get_parser() != nullptr) {
				current_class = base_ref->get_parser()->get_tree();
				continue;
			}
			current_class = nullptr;
			continue;
		}

		const StringName native_type = current_class->base_type.native_type != StringName()
				? current_class->base_type.native_type
				: (current_class->base_type.kind == BSParser::DataType::NATIVE ? current_class->base_type.native_type : StringName());
		if (native_type != StringName()) {
			MethodInfo info;
			if (BSNativeDB::get_method_info(native_type, p_function_name, &info)) {
				r_implementation.method_info = info;
				r_implementation.method_info_source = vformat(R"(Implementation comes from native class "%s".)", native_type);
				r_implementation.has_method_info = true;
				return true;
			}
		}
		current_class = nullptr;
	}

	return false;
}

void BSAnalyzer::validate_trait_requirements(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->is_trait || p_class->is_abstract) {
		return;
	}

	// Foundry validate_trait_requirements @ c9d5e35: only abstract trait methods are requirements;
	// concrete trait methods do not satisfy another trait's abstracts.
	HashSet<StringName> missing_trait_methods;
	for (int t = 0; t < p_class->resolved_traits.size(); t++) {
		BSParser::ClassNode *trait = p_class->resolved_traits[t];
		if (trait == nullptr) {
			continue;
		}
		analyze_class_interface(trait);
		for (int i = 0; i < trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = trait->members[i];
			if (member.type != BSParser::ClassNode::Member::FUNCTION ||
					member.function == nullptr || !member.function->is_abstract ||
					member.function->identifier == nullptr) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (!find_trait_implementation(p_class, member.function->identifier->name, implementation)) {
				const StringName function_name = member.function->identifier->name;
				if (!missing_trait_methods.has(function_name)) {
					missing_trait_methods.insert(function_name);
					push_error(vformat(R"*(Class "%s" must implement trait method "%s.%s()".)*",
									   bs_class_or_trait_diagnostic_name(p_class), bs_class_or_trait_diagnostic_name(trait),
									   function_name),
							_trait_requirement_source(p_class, trait));
				}
				continue;
			}
			// Signature substitution / MethodInfo comparison remains follow-up under #60.
			(void)implementation;
		}
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		if (p_class->members[i].type == BSParser::ClassNode::Member::CLASS) {
			validate_trait_requirements(p_class->members[i].m_class);
		}
	}
}

BSParser::ClassNode *BSAnalyzer::resolve_builtin_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_builtin_type) {
	if (p_conformance->builtin_target_shim != nullptr) {
		return p_conformance->builtin_target_shim;
	}

	BSParser::ClassNode *shim = parser->alloc_recovery_node<BSParser::ClassNode>();
	shim->fqcn = String(Variant::get_type_name(p_builtin_type.builtin_type));
	shim->resolved_interface = true;
	shim->resolved_body = true;
	shim->resolved_trait_uses = true;
	shim->is_builtin_conformance_shim = true;

	BSParser::DataType self_type = p_builtin_type;
	self_type.is_meta_type = false;
	if (self_type.type_source == BSParser::DataType::UNDETECTED) {
		self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	}
	shim->set_datatype(self_type);

	p_conformance->builtin_target_shim = shim;
	return shim;
}

BSParser::ClassNode *BSAnalyzer::resolve_native_conformance_shim(BSParser::ConformanceNode *p_conformance, const BSParser::DataType &p_native_type) {
	if (p_conformance->native_target_shim != nullptr) {
		return p_conformance->native_target_shim;
	}

	BSParser::ClassNode *shim = parser->alloc_recovery_node<BSParser::ClassNode>();
	shim->fqcn = String(p_native_type.native_type);
	shim->extends_used = true;
	shim->base_type = p_native_type;
	shim->base_type.is_meta_type = false;
	if (shim->base_type.type_source == BSParser::DataType::UNDETECTED) {
		shim->base_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	}
	shim->resolved_interface = true;
	shim->resolved_body = true;
	shim->resolved_trait_uses = true;
	shim->is_native_conformance_shim = true;

	BSParser::DataType self_type;
	self_type.kind = BSParser::DataType::CLASS;
	self_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.class_type = shim;
	self_type.native_type = p_native_type.native_type;
	self_type.is_meta_type = true;
	shim->set_datatype(self_type);

	p_conformance->native_target_shim = shim;
	return shim;
}

BSParser::ClassNode *BSAnalyzer::resolve_conformance_target(BSParser::ConformanceNode *p_conformance, BSParser::DataType &r_target_type) {
	if (p_conformance == nullptr || p_conformance->target == nullptr) {
		return nullptr;
	}

	// Prefer same-file class identity before index / ScriptServer lookups so `extend Head` binds the
	// parse tree under analysis (declaration commit may not have class_type yet).
	if (!p_conformance->target->type_chain.is_empty() && p_conformance->target->type_chain.size() == 1) {
		const StringName name = p_conformance->target->type_chain[0]->name;
		BSParser::ClassNode *local = _find_local_class_by_name(parser->get_tree(), name);
		if (local != nullptr && !local->is_trait) {
			r_target_type.kind = BSParser::DataType::CLASS;
			r_target_type.class_type = local;
			r_target_type.builtin_type = Variant::OBJECT;
			r_target_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			r_target_type.native_type = local->base_type.native_type;
			p_conformance->target->set_datatype(r_target_type);
			return local;
		}
	}

	r_target_type = datatype_from_type_node(p_conformance->target);
	p_conformance->target->set_datatype(r_target_type);

	if (r_target_type.kind == BSParser::DataType::CLASS && r_target_type.class_type != nullptr) {
		return r_target_type.class_type;
	}
	if ((r_target_type.kind == BSParser::DataType::CLASS || r_target_type.kind == BSParser::DataType::SCRIPT) &&
			!r_target_type.script_path.is_empty()) {
		Error err = OK;
		Ref<BSParserRef> ref = BSCache::get_parser(r_target_type.script_path, BSParserRef::INTERFACE_SOLVED, err,
				parser != nullptr ? parser->script_path : String());
		if (ref.is_valid() && err == OK && ref->get_parser() != nullptr) {
			return ref->get_parser()->get_tree();
		}
	}

	if (r_target_type.kind == BSParser::DataType::NATIVE && r_target_type.native_type != StringName() &&
			!r_target_type.is_meta_type) {
		Engine *engine = Engine::get_singleton();
		if (engine != nullptr && engine->has_singleton(r_target_type.native_type)) {
			push_error(vformat(R"(Cannot retroactively conform engine singleton "%s" to a trait.)", r_target_type.native_type), p_conformance->target);
			return nullptr;
		}
		return resolve_native_conformance_shim(p_conformance, r_target_type);
	}

	if (r_target_type.kind == BSParser::DataType::BUILTIN) {
		if (r_target_type.builtin_type == Variant::NIL || r_target_type.builtin_type == Variant::OBJECT) {
			push_error(vformat(R"(Cannot retroactively conform "%s" to a trait.)", Variant::get_type_name(r_target_type.builtin_type)), p_conformance->target);
			return nullptr;
		}
		return resolve_builtin_conformance_shim(p_conformance, r_target_type);
	}

	if (r_target_type.kind == BSParser::DataType::VARIANT) {
		push_error(R"(Retroactive conformance supports only BaristaScript class, native engine-class, and builtin value-type targets.)", p_conformance->target);
		return nullptr;
	}

	if (!r_target_type.is_variant()) {
		push_error(R"(Retroactive conformance supports only BaristaScript class, native engine-class, and builtin value-type targets.)", p_conformance->target);
	}
	return nullptr;
}

BSParser::ClassNode *BSAnalyzer::resolve_conformance_trait_use(BSParser::ClassNode *p_scope, BSParser::ClassNode::TraitUse &p_trait_use, const BSParser::Node *p_source) {
	const String name = p_trait_use.to_string();
	if (name.is_empty()) {
		return nullptr;
	}
	if (!p_trait_use.type_arguments.is_empty()) {
		push_error("Generic trait specialization is not available until M5.", p_source);
		return nullptr;
	}

	BSParser::ClassNode *trait = p_trait_use.resolved_trait;
	if (trait == nullptr) {
		trait = _find_local_trait_by_name(p_scope, name);
	}
	if (trait == nullptr) {
		BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
		BSDeclarationRecord record;
		bool found = false;
		if (language != nullptr) {
			found = language->try_resolve_declaration(name, record);
			if (!found && p_scope != nullptr && !p_scope->namespace_name.is_empty()) {
				found = language->try_resolve_declaration(p_scope->namespace_name + String(".") + name, record);
			}
			if (!found && p_scope != nullptr) {
				for (int j = 0; j < p_scope->imports.size(); j++) {
					found = language->try_resolve_declaration(p_scope->imports[j] + String(".") + name, record);
					if (found) {
						break;
					}
				}
			}
		}
		if (!found || record.kind != BSDeclarationKind::TRAIT) {
			push_error(vformat(R"(Could not find trait "%s".)", name), p_source);
			return nullptr;
		}
		Error err = OK;
		Ref<BSParserRef> trait_ref = BSCache::get_parser(record.path, BSParserRef::INTERFACE_SOLVED, err,
				parser != nullptr ? parser->script_path : String());
		if (trait_ref.is_null() || err != OK || trait_ref->get_parser() == nullptr || trait_ref->get_parser()->get_tree() == nullptr) {
			push_error(vformat(R"(Could not resolve trait "%s".)", name), p_source);
			return nullptr;
		}
		trait = trait_ref->get_parser()->get_tree();
	}
	if (trait == nullptr || !trait->is_trait) {
		push_error(vformat(R"("%s" is not a trait.)", name), p_source);
		return nullptr;
	}

	p_trait_use.resolved_trait = trait;
	resolve_used_traits(trait);
	analyze_class_interface(trait);
	return trait;
}

bool BSAnalyzer::validate_conformance(BSParser::ConformanceNode *p_conformance, BSParser::ClassNode *p_target,
		BSParser::ClassNode *p_trait) {
	if (p_conformance == nullptr || p_target == nullptr || p_trait == nullptr) {
		return false;
	}

	HashMap<StringName, BSParser::FunctionNode *> witnesses_by_name;
	for (int i = 0; i < p_conformance->witnesses.size(); i++) {
		BSParser::FunctionNode *witness = p_conformance->witnesses[i];
		if (witness != nullptr && witness->identifier != nullptr) {
			witnesses_by_name.insert(witness->identifier->name, witness);
		}
	}

	bool valid = true;
	HashSet<StringName> missing_methods;

	Vector<BSParser::ClassNode *> requirement_traits;
	requirement_traits.push_back(p_trait);
	for (int i = 0; i < p_trait->resolved_traits.size(); i++) {
		BSParser::ClassNode *transitive = p_trait->resolved_traits[i];
		if (transitive == nullptr) {
			continue;
		}
		bool already = false;
		for (int j = 0; j < requirement_traits.size(); j++) {
			if (requirement_traits[j] == transitive) {
				already = true;
				break;
			}
		}
		if (!already) {
			requirement_traits.push_back(transitive);
		}
	}

	for (int t = 0; t < requirement_traits.size(); t++) {
		BSParser::ClassNode *requirement_trait = requirement_traits[t];
		analyze_class_interface(requirement_trait);

		for (int i = 0; i < requirement_trait->members.size(); i++) {
			const BSParser::ClassNode::Member &member = requirement_trait->members[i];
			if (member.type != BSParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
				continue;
			}
			BSParser::FunctionNode *required = member.function;
			const StringName function_name = required->identifier != nullptr ? required->identifier->name : StringName();
			if (function_name == StringName()) {
				continue;
			}

			if (witnesses_by_name.has(function_name)) {
				continue;
			}

			if (!required->is_abstract) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (find_trait_implementation(p_target, function_name, implementation)) {
				(void)implementation;
				continue;
			}

			if (!missing_methods.has(function_name)) {
				missing_methods.insert(function_name);
				push_error(vformat(R"*(Conformance of "%s" to trait "%s" must implement trait method "%s.%s()".)*",
								   bs_class_or_trait_diagnostic_name(p_target), bs_class_or_trait_diagnostic_name(p_trait),
								   bs_class_or_trait_diagnostic_name(requirement_trait), function_name),
						p_conformance);
			}
			valid = false;
		}
	}

	return valid;
}

void BSAnalyzer::resolve_conformances(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->conformances.is_empty()) {
		return;
	}

	BSParser::ClassNode *head = parser != nullptr ? parser->get_tree() : p_class;
	for (int conformance_index = 0; conformance_index < p_class->conformances.size(); conformance_index++) {
		BSParser::ConformanceNode *conformance = p_class->conformances[conformance_index];
		if (conformance == nullptr) {
			continue;
		}

		BSParser::DataType target_type;
		BSParser::ClassNode *target = resolve_conformance_target(conformance, target_type);
		if (target == nullptr) {
			continue;
		}

		if (!target->is_native_conformance_shim && !target->is_builtin_conformance_shim) {
			resolve_used_traits(target);
			analyze_class_interface(target);
		}

		for (int i = 0; i < conformance->traits.size(); i++) {
			BSParser::ClassNode::TraitUse &trait_use = conformance->traits.write[i];
			BSParser::ClassNode *trait = resolve_conformance_trait_use(head, trait_use, conformance);
			if (trait == nullptr) {
				continue;
			}

			// Coherence: a conformance redundant with the target's own `uses` is rejected.
			bool redundant = false;
			for (int t = 0; t < target->resolved_traits.size(); t++) {
				BSParser::ClassNode *owned_trait = target->resolved_traits[t];
				if (owned_trait == trait || (owned_trait != nullptr && owned_trait->fqcn == trait->fqcn && !trait->fqcn.is_empty())) {
					redundant = true;
					break;
				}
			}
			if (redundant) {
				push_error(vformat(R"(Class "%s" already conforms to trait "%s" through its own "uses"; the conformance is redundant.)",
								   bs_class_or_trait_diagnostic_name(target), bs_class_or_trait_diagnostic_name(trait)),
						conformance);
				continue;
			}

			validate_conformance(conformance, target, trait);
		}
	}
}

void BSAnalyzer::resolve_conformance_bodies(BSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->conformances.is_empty()) {
		return;
	}

	for (int c = 0; c < p_class->conformances.size(); c++) {
		BSParser::ConformanceNode *conformance = p_class->conformances[c];
		if (conformance == nullptr || conformance->target == nullptr) {
			continue;
		}

		const BSParser::DataType target_type = conformance->target->get_datatype();
		BSParser::ClassNode *target = nullptr;
		if (target_type.kind == BSParser::DataType::CLASS && target_type.class_type != nullptr) {
			target = target_type.class_type;
		} else if ((target_type.kind == BSParser::DataType::CLASS || target_type.kind == BSParser::DataType::SCRIPT) &&
				!target_type.script_path.is_empty()) {
			Error err = OK;
			Ref<BSParserRef> ref = BSCache::get_parser(target_type.script_path, BSParserRef::INTERFACE_SOLVED, err,
					parser != nullptr ? parser->script_path : String());
			if (ref.is_valid() && err == OK && ref->get_parser() != nullptr) {
				target = ref->get_parser()->get_tree();
			}
		} else if (target_type.kind == BSParser::DataType::NATIVE) {
			target = conformance->native_target_shim;
		} else if (target_type.kind == BSParser::DataType::BUILTIN) {
			target = conformance->builtin_target_shim;
		}
		if (target == nullptr) {
			continue;
		}

		BSParser::ClassNode *previous_class = current_class;
		current_class = target;
		for (int i = 0; i < conformance->witnesses.size(); i++) {
			BSParser::FunctionNode *witness = conformance->witnesses[i];
			if (witness == nullptr) {
				continue;
			}
			if (witness->return_type != nullptr) {
				witness->set_datatype(datatype_from_type_node(witness->return_type));
			}
			for (int p = 0; p < witness->parameters.size(); p++) {
				BSParser::ParameterNode *parameter = witness->parameters[p];
				if (parameter != nullptr && parameter->datatype_specifier != nullptr) {
					parameter->set_datatype(datatype_from_type_node(parameter->datatype_specifier));
				}
			}
			analyze_function_body(witness);
		}
		current_class = previous_class;
	}
}

} // namespace barista_script
