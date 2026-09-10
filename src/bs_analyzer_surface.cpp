/**************************************************************************/
/*  bs_analyzer_surface.cpp                                               */
/*                                                                        */
/*  #60 class-body surface diagnostics. Ports unused-private /            */
/*  unused-signal post-pass, built-in/custom resolve_annotation,          */
/*  resolve_enum_values, complete_self_referential_enum_type /            */
/*  enum_type_argument_bindings / specialize_enum_type, same-file scope   */
/*  inheritance helpers, CLASS inheritance member bind, and               */
/*  resolve_class_member with external OwnerResolutionFailures /          */
/*  DependentResolutionFailureReplays / ForeignAnalyzerVisibilityScope    */
/*  (@ c9d5e35). Class-phase INTERFACE/BODY foreign recording/replay lives*/
/*  in analyze_class_interface/body (`bs_analyzer.cpp`);                  */
/*  ForeignAnalyzerVisibilityScope installs                               */
/*  BSConformanceRegistry::ScopedVisibility for owner ConformanceVisibility.*/
/*  FS* -> BS*; engine contact through bs_platform.h.                     */
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
#include "bs_type.h"

namespace barista_script {

// Use the engine's complete conversion surface after Foundry's strict-admission gate
// (core/variant/variant_utility.cpp:853 @ c9d5e35), including Array/packed-array conversions.
static bool _convert_annotation_argument(Variant &r_value, Variant::Type p_expected_type) {
	if (r_value.get_type() == p_expected_type) {
		return true;
	}
	if (!Variant::can_convert_strict(r_value.get_type(), p_expected_type)) {
		return false;
	}
	Variant converted = UtilityFunctions::type_convert(r_value, p_expected_type);
	if (converted.get_type() != p_expected_type) {
		return false;
	}
	r_value = converted;
	return true;
}

static String _annotation_target_name(uint32_t p_target_kind) {
	switch (p_target_kind) {
		case BSParser::AnnotationDeclarationNode::TARGET_CLASS:
			return "a class";
		case BSParser::AnnotationDeclarationNode::TARGET_METHOD:
			return "a method";
		case BSParser::AnnotationDeclarationNode::TARGET_VARIABLE:
			return "a variable";
		case BSParser::AnnotationDeclarationNode::TARGET_SIGNAL:
			return "a signal";
		case BSParser::AnnotationDeclarationNode::TARGET_CONSTANT:
			return "a constant";
		case BSParser::AnnotationDeclarationNode::TARGET_PARAMETER:
			return "a parameter";
		default:
			return "this target";
	}
}

bool BSAnalyzer::coerce_annotation_argument(const BSParser::DataType &p_parameter_type, Variant &r_value,
		const BSParser::ExpressionNode *p_argument, const String &p_context) {
	if (p_parameter_type.kind != BSParser::DataType::BUILTIN) {
		return true;
	}
	const Variant::Type expected_type = p_parameter_type.builtin_type;
	if (expected_type == Variant::NIL || r_value.get_type() == expected_type) {
		return true;
	}
#ifdef DEBUG_ENABLED
	if (expected_type == Variant::INT && r_value.get_type() == Variant::FLOAT) {
		Vector<String> symbols;
		push_warning(p_argument, BSWarning::NARROWING_CONVERSION, symbols);
	}
#endif
	if (!_convert_annotation_argument(r_value, expected_type)) {
		push_error(vformat(R"(Invalid %s: expected "%s" but got "%s".)", p_context,
						   Variant::get_type_name(expected_type), Variant::get_type_name(r_value.get_type())),
				p_argument);
		return false;
	}
	return true;
}

void BSAnalyzer::resolve_annotation_declaration(BSParser::AnnotationDeclarationNode *p_declaration) {
	if (p_declaration == nullptr || p_declaration->resolved_signature) {
		return;
	}
	p_declaration->resolved_signature = true;

	BSParser::ClassNode *previous_class = current_class;
	current_class = parser->get_tree();
	Vector<BSParser::ParameterNode *> parameters = p_declaration->parameters;
	if (p_declaration->rest_parameter != nullptr) {
		parameters.push_back(p_declaration->rest_parameter);
	}
	for (BSParser::ParameterNode *parameter : parameters) {
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		if (parameter->datatype_specifier == nullptr) {
			push_error(vformat(R"(Annotation parameter "%s" must declare a type.)", parameter->identifier->name), parameter);
			BSParser::DataType variant_type;
			variant_type.kind = BSParser::DataType::VARIANT;
			variant_type.type_source = BSParser::DataType::INFERRED;
			parameter->set_datatype(variant_type);
		} else {
			parameter->set_datatype(datatype_from_type_node(parameter->datatype_specifier));
		}
		if (parameter->initializer != nullptr) {
			reduce_expression(parameter->initializer);
			if (!has_materialized_constant_value(parameter->initializer)) {
				push_error(vformat(R"(Default value for annotation parameter "%s" must be a constant expression.)", parameter->identifier->name), parameter->initializer);
			} else {
				Variant default_value = parameter->initializer->reduced_value;
				const String context = vformat(R"(default value of annotation parameter "%s")", parameter->identifier->name);
				coerce_annotation_argument(parameter->get_datatype(), default_value, parameter->initializer, context);
			}
		}
	}
	current_class = previous_class;
}

void BSAnalyzer::resolve_annotation_declaration_signatures() {
	if (parser == nullptr || parser->get_tree() == nullptr) {
		return;
	}
	for (BSParser::AnnotationDeclarationNode *declaration : parser->get_tree()->annotation_declarations) {
		resolve_annotation_declaration(declaration);
	}
}

BSParser::AnnotationDeclarationNode *BSAnalyzer::load_external_annotation_declaration(
		const String &p_qualified_name, BSParser::AnnotationNode *p_annotation, bool &r_error_reported) {
	r_error_reported = false;
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language == nullptr || parser == nullptr) {
		return nullptr;
	}
	Vector<String> paths = language->get_declaration_index().get_annotation_declaring_paths(p_qualified_name);
	if (paths.size() > 1) {
		push_error(vformat(R"(Ambiguous annotation "%s": the canonical identity "%s" is declared in multiple files.)",
						   p_annotation->name, p_qualified_name),
				p_annotation);
		r_error_reported = true;
		return nullptr;
	}
	if (paths.is_empty()) {
		return nullptr;
	}
	const String path = paths[0].simplify_path();
	if (path == parser->script_path.simplify_path()) {
		return nullptr;
	}
	if (!is_bootstrap_path_allowed(path)) {
		push_error(vformat(R"(Build task bootstrap cannot use annotation "%s" from "%s"; it is outside the provider bootstrap root "%s".)",
						   p_qualified_name, path, bootstrap_root_storage()),
				p_annotation);
		r_error_reported = true;
		return nullptr;
	}

	Ref<BSParserRef> ref = parser->get_depended_parser_for(path);
	if (ref.is_null()) {
		return nullptr;
	}
	const Error err = ref->raise_status(BSParserRef::INTERFACE_SOLVED);
	if (err != OK || ref->get_status() < BSParserRef::INTERFACE_SOLVED) {
		return nullptr;
	}
	BSParser *external_parser = ref->get_parser();
	if (external_parser == nullptr || external_parser->get_tree() == nullptr) {
		return nullptr;
	}
	BSParser::AnnotationDeclarationNode *first_match = nullptr;
	for (BSParser::AnnotationDeclarationNode *declaration : external_parser->get_tree()->annotation_declarations) {
		if (declaration->qualified_name != p_qualified_name) {
			continue;
		}
		if (first_match != nullptr) {
			push_error(vformat(R"(Ambiguous annotation "%s": the canonical identity "%s" has multiple declarations.)",
							   p_annotation->name, p_qualified_name),
					p_annotation);
			r_error_reported = true;
			return nullptr;
		}
		first_match = declaration;
	}
	return first_match;
}

BSParser::AnnotationDeclarationNode *BSAnalyzer::resolve_qualified_annotation_declaration(
		const String &p_identity, BSParser::AnnotationNode *p_annotation) {
	for (BSParser::AnnotationDeclarationNode *declaration : parser->get_tree()->annotation_declarations) {
		if (declaration->qualified_name == p_identity) {
			resolve_annotation_declaration(declaration);
			return declaration;
		}
	}
	bool error_reported = false;
	BSParser::AnnotationDeclarationNode *declaration = load_external_annotation_declaration(p_identity, p_annotation, error_reported);
	if (declaration != nullptr || error_reported) {
		return declaration;
	}
	push_error(vformat(R"(Unknown annotation "%s". A fully qualified annotation must name an existing declaration.)", p_annotation->name), p_annotation);
	return nullptr;
}

BSParser::AnnotationDeclarationNode *BSAnalyzer::resolve_custom_annotation_declaration(BSParser::AnnotationNode *p_annotation) {
	const String usage_name = String(p_annotation->name);
	const String short_name = usage_name.begins_with("@") ? usage_name.substr(1) : usage_name;
	if (short_name.find(".") >= 0) {
		return resolve_qualified_annotation_declaration(short_name, p_annotation);
	}

	for (BSParser::AnnotationDeclarationNode *declaration : parser->get_tree()->annotation_declarations) {
		if (declaration->identifier != nullptr && declaration->identifier->name == StringName(short_name)) {
			resolve_annotation_declaration(declaration);
			return declaration;
		}
	}

	const String current_namespace = parser->get_tree()->namespace_name;
	const String own_identity = current_namespace.is_empty() ? short_name : current_namespace + String(".") + short_name;
	bool error_reported = false;
	BSParser::AnnotationDeclarationNode *declaration = load_external_annotation_declaration(own_identity, p_annotation, error_reported);
	if (declaration != nullptr || error_reported) {
		return declaration;
	}

	Vector<String> matching_namespaces;
	String resolved_identity;
	HashSet<String> checked_imports;
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	if (language != nullptr) {
		for (const String &import : parser->get_tree()->imports) {
			if (checked_imports.has(import)) {
				continue;
			}
			checked_imports.insert(import);
			const String identity = import + String(".") + short_name;
			if (!language->get_declaration_index().get_annotation_declaring_paths(identity).is_empty()) {
				matching_namespaces.push_back(import);
				resolved_identity = identity;
			}
		}
	}
	if (matching_namespaces.size() > 1) {
		matching_namespaces.sort();
		String namespace_list;
		for (int i = 0; i < matching_namespaces.size(); i++) {
			if (i > 0) {
				namespace_list += i == matching_namespaces.size() - 1 ? " and " : ", ";
			}
			namespace_list += "\"" + matching_namespaces[i] + "\"";
		}
		push_error(vformat(R"(Ambiguous annotation "%s": it is declared in imported namespaces %s.)", p_annotation->name, namespace_list), p_annotation);
		return nullptr;
	}
	if (matching_namespaces.size() == 1) {
		error_reported = false;
		declaration = load_external_annotation_declaration(resolved_identity, p_annotation, error_reported);
		if (declaration != nullptr || error_reported) {
			return declaration;
		}
	}

	push_error(vformat(R"(Unknown annotation "%s". Custom annotations must be declared in the current namespace or an imported namespace.)", p_annotation->name), p_annotation);
	return nullptr;
}

void BSAnalyzer::resolve_custom_annotation(BSParser::AnnotationNode *p_annotation, uint32_t p_target_kind) {
	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;
	BSParser::AnnotationDeclarationNode *declaration = resolve_custom_annotation_declaration(p_annotation);
	if (declaration == nullptr) {
		return;
	}
	p_annotation->resolved_qualified_name = declaration->qualified_name;
	if (p_target_kind == 0 || (declaration->targets & p_target_kind) == 0) {
		push_error(vformat(R"(Annotation "%s" cannot be applied to %s.)", p_annotation->name, _annotation_target_name(p_target_kind)), p_annotation);
	}

	const int fixed_count = declaration->parameters.size();
	const bool is_variadic = declaration->is_variadic();
	LocalVector<int> binding;
	binding.resize(fixed_count);
	for (int i = 0; i < fixed_count; i++) {
		binding[i] = 0;
	}
	int next_positional = 0;
	bool seen_named = false;
	bool reported_too_many = false;
	bool argument_error = false;
	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		BSParser::ExpressionNode *argument = p_annotation->arguments[i];
		if (argument == nullptr) {
			argument_error = true;
			continue;
		}
		const StringName argument_name = i < p_annotation->argument_names.size() ? p_annotation->argument_names[i] : StringName();
		reduce_expression(argument);
		if (!has_materialized_constant_value(argument)) {
			push_error(vformat(R"(Argument %d of annotation "%s" is not a constant expression.)", i + 1, p_annotation->name), argument);
			argument_error = true;
			continue;
		}
		Variant value = argument->reduced_value;
		BSParser::ParameterNode *parameter = nullptr;
		if (argument_name == StringName()) {
			if (seen_named) {
				push_error(vformat(R"(Positional argument after named argument in annotation "%s".)", p_annotation->name), argument);
				argument_error = true;
				continue;
			}
			if (next_positional < fixed_count) {
				binding[next_positional] = 1;
				parameter = declaration->parameters[next_positional];
			} else if (is_variadic) {
				parameter = declaration->rest_parameter;
			} else {
				if (!reported_too_many) {
					push_error(vformat(R"(Annotation "%s" takes at most %d argument(s), but %d were given.)",
									   p_annotation->name, fixed_count, p_annotation->arguments.size()),
							argument);
					reported_too_many = true;
				}
				argument_error = true;
				continue;
			}
			next_positional++;
		} else {
			seen_named = true;
			const int *parameter_index = declaration->parameters_indices.getptr(argument_name);
			if (parameter_index == nullptr) {
				push_error(vformat(R"(Annotation "%s" has no parameter named "%s".)", p_annotation->name, argument_name), argument);
				argument_error = true;
				continue;
			}
			if (binding[*parameter_index] != 0) {
				push_error(vformat(R"(Parameter "%s" of annotation "%s" was specified more than once.)", argument_name, p_annotation->name), argument);
				argument_error = true;
				continue;
			}
			binding[*parameter_index] = 2;
			parameter = declaration->parameters[*parameter_index];
		}
		if (parameter != nullptr) {
			const String context = vformat(R"(argument %d of annotation "%s")", i + 1, p_annotation->name);
			if (!coerce_annotation_argument(parameter->get_datatype(), value, argument, context)) {
				argument_error = true;
				continue;
			}
		}
		p_annotation->resolved_arguments.push_back(value);
	}
	if (!argument_error) {
		for (int i = 0; i < fixed_count; i++) {
			if (binding[i] == 0 && declaration->parameters[i]->initializer == nullptr) {
				push_error(vformat(R"(Annotation "%s" is missing required argument "%s".)",
								   p_annotation->name, declaration->parameters[i]->identifier->name),
						p_annotation);
			}
		}
	}
}

Error BSAnalyzer::validate_annotation_declarations() {
	if (parser == nullptr || parser->get_tree() == nullptr) {
		return ERR_BUG;
	}
	HashSet<String> declared_in_file;
	BaristaScriptLanguage *language = BaristaScriptLanguage::get_singleton();
	for (BSParser::AnnotationDeclarationNode *declaration : parser->get_tree()->annotation_declarations) {
		if (declaration == nullptr || declaration->identifier == nullptr) {
			continue;
		}
		const StringName short_name = declaration->identifier->name;
		if (parser->valid_annotations.has(StringName("@" + String(short_name)))) {
			push_error(vformat(R"(Cannot declare custom annotation "%s": "@%s" is a built-in annotation.)", short_name, short_name), declaration);
			continue;
		}
		const String &qualified_name = declaration->qualified_name;
		if (qualified_name.is_empty()) {
			continue;
		}
		if (declared_in_file.has(qualified_name)) {
			push_error(vformat(R"(Duplicate annotation declaration "%s".)", qualified_name), declaration);
			continue;
		}
		declared_in_file.insert(qualified_name);
		if (language != nullptr) {
			const Vector<String> paths = language->get_declaration_index().get_annotation_declaring_paths(qualified_name);
			int other_paths = 0;
			for (const String &path : paths) {
				if (path.simplify_path() != parser->script_path.simplify_path()) {
					other_paths++;
				}
			}
			if (other_paths > 0) {
				push_error(vformat(R"(Duplicate annotation declaration "%s": the same canonical annotation is declared in another file.)", qualified_name), declaration);
			}
		}
	}
	return parser->get_errors().is_empty() ? OK : ERR_PARSE_ERROR;
}

// Static annotation validation from Foundry c9d5e35:4112-4248. Project-global
// autoload indexing/ordering and singleton execution remain outside M3.
void BSAnalyzer::resolve_autoload_annotation(BSParser::AnnotationNode *p_annotation) {
	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;
	const int previous_errors = parser->get_errors().size();
	BSParser::ClassNode *head = parser->get_tree();
	bool valid = true;
	int count = 0;
	for (BSParser::AnnotationNode *annotation : head->annotations) {
		if (annotation != nullptr && annotation->name == SNAME("@autoload")) {
			count++;
		}
	}
	if (count > 1) {
		push_error(R"("@autoload" annotation can only be used once per script.)", p_annotation);
		valid = false;
	}
	if (head->identifier == nullptr || head->trait_name_used || head->is_enum_file || head->is_tuple_file) {
		push_error(R"("@autoload" requires "class_name".)", p_annotation);
		valid = false;
	}
	// Direct interface consumers must have the same ultimate native-base evidence
	// as the ordinary inheritance-first analyzer path, including foreign bases.
	if (!head->base_type.is_resolving()) {
		resolve_class_inheritance(head);
	}
	const StringName native_base = head->base_type.native_type;
	if (native_base == StringName() || !ClassDB::is_parent_class(native_base, SNAME("Node"))) {
		push_error(R"("@autoload" requires the script to inherit from "Node".)", p_annotation);
		valid = false;
	}
	Array dependency_names;
	int64_t order = 0;
	bool bound[2] = { false, false };
	bool seen_named = false;
	int next_positional = 0;
	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		BSParser::ExpressionNode *argument = p_annotation->arguments[i];
		const StringName name = i < p_annotation->argument_names.size() ? p_annotation->argument_names[i] : StringName();
		int slot = -1;
		if (name == StringName()) {
			if (seen_named) {
				push_error(R"(Positional argument after named argument in annotation "@autoload".)", argument);
				valid = false;
				continue;
			}
			slot = next_positional++;
		} else {
			seen_named = true;
			if (name == SNAME("depends_on")) {
				slot = 0;
			} else if (name == SNAME("order_id")) {
				slot = 1;
			} else {
				push_error(vformat(R"(Annotation "@autoload" has no parameter named "%s".)", name), argument);
				valid = false;
				continue;
			}
		}
		if (slot >= 2) {
			push_error(vformat(R"(Annotation "@autoload" takes at most 2 argument(s), but %d were given.)", p_annotation->arguments.size()), argument);
			valid = false;
			continue;
		}
		if (bound[slot]) {
			push_error(vformat(R"(Parameter "%s" of annotation "@autoload" was specified more than once.)", slot == 0 ? "depends_on" : "order_id"), argument);
			valid = false;
			continue;
		}
		bound[slot] = true;
		reduce_expression(argument);
		if (slot == 0) {
			if (argument->type != BSParser::Node::ARRAY) {
				push_error(R"(Argument "depends_on" of annotation "@autoload" must be an array of class names.)", argument);
				valid = false;
				continue;
			}
			const auto *dependencies = static_cast<BSParser::ArrayNode *>(argument);
			for (int d = 0; d < dependencies->elements.size(); d++) {
				const BSParser::ExpressionNode *dependency = dependencies->elements[d];
				const BSParser::DataType type = dependency->get_datatype();
				// #140's reduction owns candidate authority and the foreign parser lifetime.
				// Never revive a rejected name from a raw ScriptServer/index hint.
				StringName identity;
				if (type.is_meta_type && type.kind == BSParser::DataType::CLASS && type.class_type != nullptr) {
					identity = type.class_type->get_global_name();
				}
				if (identity == StringName()) {
					push_error(vformat(R"(Dependency %d of annotation "@autoload" must resolve to a script class.)", d + 1), dependency);
					valid = false;
				} else {
					dependency_names.push_back(identity);
				}
			}
		} else {
			if (!has_materialized_constant_value(argument) || argument->reduced_value.get_type() != Variant::INT) {
				push_error(R"(Argument "order_id" of annotation "@autoload" must be a constant integer expression.)", argument);
				valid = false;
				continue;
			}
			const int64_t value = argument->reduced_value;
			if (value < INT32_MIN || value > INT32_MAX) {
				push_error(R"("order_id" of annotation "@autoload" must fit in a 32-bit signed integer.)", argument);
				valid = false;
				continue;
			}
			order = value;
		}
	}
	if (valid && parser->get_errors().size() == previous_errors) {
		p_annotation->resolved_arguments.push_back(dependency_names);
		p_annotation->resolved_arguments.push_back(order);
	}
}

void BSAnalyzer::resolve_annotation(BSParser::AnnotationNode *p_annotation, uint32_t p_target_kind) {
	if (p_annotation == nullptr) {
		return;
	}
	if (p_annotation->name == SNAME("@autoload")) {
		resolve_autoload_annotation(p_annotation);
		return;
	}
	if (p_annotation->is_custom) {
		resolve_custom_annotation(p_annotation, p_target_kind);
		return;
	}
	ERR_FAIL_COND_MSG(parser == nullptr || !parser->valid_annotations.has(p_annotation->name),
			vformat(R"(Annotation "%s" not found to validate.)", p_annotation->name));

	if (p_annotation->is_resolved) {
		return;
	}
	p_annotation->is_resolved = true;

	// Parser already resolved literal arguments for a few annotations (@icon, region ignores).
	if (!p_annotation->resolved_arguments.is_empty()) {
		return;
	}

	const MethodInfo &annotation_info = parser->valid_annotations[p_annotation->name].info;
	for (int64_t i = 0, j = 0; i < p_annotation->arguments.size(); i++) {
		BSParser::ExpressionNode *argument = p_annotation->arguments[i];
		if (argument == nullptr) {
			continue;
		}
		const PropertyInfo &argument_info = annotation_info.arguments[j];
		if (j + 1 < annotation_info.arguments.size()) {
			++j;
		}

		reduce_expression(argument);

		if (!has_materialized_constant_value(argument)) {
			push_error(vformat(R"(Argument %d of annotation "%s" isn't a constant expression.)", i + 1, p_annotation->name), argument);
			return;
		}

		Variant value = argument->reduced_value;
		if (value.get_type() != argument_info.type) {
#ifdef DEBUG_ENABLED
			if (argument_info.type == Variant::INT && value.get_type() == Variant::FLOAT) {
				Vector<String> symbols;
				push_warning(argument, BSWarning::NARROWING_CONVERSION, symbols);
			}
#endif
			if (!_convert_annotation_argument(value, argument_info.type)) {
				push_error(vformat(R"(Invalid argument for annotation "%s": argument %d should be "%s" but is "%s".)",
								   p_annotation->name, i + 1, Variant::get_type_name(argument_info.type), argument->get_datatype().to_string()),
						argument);
				return;
			}
		}

		p_annotation->resolved_arguments.push_back(value);
	}
}

void BSAnalyzer::warn_unused_class_members(BSParser::ClassNode *p_class) {
#ifdef DEBUG_ENABLED
	if (p_class == nullptr) {
		return;
	}
	// Foundry resolve_class_body unused pass @ c9d5e35: private "_" members and all signals.
	for (int i = 0; i < p_class->members.size(); i++) {
		const BSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr &&
				member.variable->identifier != nullptr) {
			if (member.variable->usages == 0 && String(member.variable->identifier->name).begins_with("_")) {
				Vector<String> symbols;
				symbols.push_back(String(member.variable->identifier->name));
				push_warning(member.variable->identifier, BSWarning::UNUSED_PRIVATE_CLASS_VARIABLE, symbols);
			}
		} else if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr &&
				member.signal->identifier != nullptr) {
			if (member.signal->usages == 0) {
				Vector<String> symbols;
				symbols.push_back(String(member.signal->identifier->name));
				push_warning(member.signal->identifier, BSWarning::UNUSED_SIGNAL, symbols);
			}
		}
		// Nested CLASS unused is handled when analyze_class_body recurses into the nested class
		// (Foundry resolve_class_body does not re-walk nested members inside the unused pass).
	}
#else
	(void)p_class;
#endif
}

#define ENUM_SEPARATOR "."

BSParser::DataType BSAnalyzer::make_class_enum_type(const StringName &p_enum_name, BSParser::ClassNode *p_class, const String &p_script_path, bool p_meta) {
	BSParser::DataType type;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = BSParser::DataType::ENUM;
	type.builtin_type = p_meta ? Variant::DICTIONARY : Variant::INT;
	type.enum_type = p_enum_name;
	type.is_constant = true;
	type.is_meta_type = p_meta;
	if (p_class != nullptr && !p_class->fqcn.is_empty()) {
		type.native_type = StringName(p_class->fqcn + ENUM_SEPARATOR + String(p_enum_name));
	} else {
		type.native_type = p_enum_name;
	}
	type.class_type = p_class;
	type.script_path = p_script_path;
	return type;
}

// Foundry make_standalone_global_enum_type @ c9d5e35: an enum_name identity is
// its declaration's global name, with no extra owning-class/member prefix.
BSParser::DataType BSAnalyzer::make_standalone_global_enum_type(BSParser::ClassNode *p_head, const String &p_script_path, bool p_meta) {
	BSParser::DataType type = make_class_enum_type(p_head->get_global_name(), nullptr, p_script_path, p_meta);
	type.class_type = p_head;
	return type;
}

BSParser::DataType BSAnalyzer::make_tuple_type(const StringName &p_tuple_name, const String &p_owner_fqcn,
		const String &p_script_path, const Vector<BSParser::DataType> &p_element_types,
		const Vector<StringName> &p_field_names, bool p_meta) {
	BSParser::DataType type;
	type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
	type.kind = BSParser::DataType::TUPLE;
	type.builtin_type = Variant::ARRAY;
	type.tuple_name = p_tuple_name;
	if (p_tuple_name != StringName()) {
		type.native_type = p_owner_fqcn.is_empty()
				? p_tuple_name
				: StringName(p_owner_fqcn + String(ENUM_SEPARATOR) + String(p_tuple_name));
	}
	type.script_path = p_script_path;
	type.container_element_types = p_element_types;
	type.tuple_field_names = p_field_names;
	type.is_meta_type = p_meta;
	type.is_constant = p_meta;
	type.is_read_only = !p_meta;
	return type;
}

BSParser::DataType BSAnalyzer::type_from_metatype(const BSParser::DataType &p_meta_type) {
	BSParser::DataType result = p_meta_type;
	result.is_meta_type = false;
	result.is_pseudo_type = false;
	if (p_meta_type.kind == BSParser::DataType::ENUM) {
		// Tagged unions erase to read-only [tag, payload...] Arrays; plain enums stay INT-backed.
		result.builtin_type = p_meta_type.is_tagged_union ? Variant::ARRAY : Variant::INT;
	} else {
		result.is_constant = false;
	}
	return result;
}

BSParser::DataType BSAnalyzer::resolve_enum_values(BSParser::EnumNode *p_enum, const BSParser::DataType &p_enum_type, BSParser::ClassNode *p_owner) {
	if (p_enum == nullptr || p_owner == nullptr) {
		return p_enum_type;
	}
	if (p_enum->get_datatype().is_set()) {
		return p_enum->get_datatype();
	}

	BSParser::ClassNode *previous_class = current_class;
	BSParser::EnumNode *previous_enum = current_enum;
	BSParser::ClassNode *previous_enum_owner = current_enum_owner;
	current_class = p_owner;
	current_enum = p_enum;
	current_enum_owner = p_owner;

	BSParser::DataType enum_type = p_enum_type;
	enum_type.is_tagged_union = p_enum->is_tagged_union;

	if (!p_enum->type_parameters.is_empty()) {
		// Generic tagged unions remain M5; still publish a shell so later references fail closed.
		push_error("Generic tagged-union specialization is not available until M5.", p_enum);
		enum_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
		enum_type.kind = BSParser::DataType::ENUM;
		p_enum->set_datatype(enum_type);
		current_enum = previous_enum;
		current_enum_owner = previous_enum_owner;
		current_class = previous_class;
		return enum_type;
	}

	// Publish identity before payload field types so a payload naming this union does not re-enter.
	if (enum_type.is_tagged_union) {
		p_enum->set_datatype(enum_type);
	}

	Dictionary dictionary;
	for (int i = 0; i < p_enum->values.size(); i++) {
		BSParser::EnumNode::Value &element = p_enum->values.write[i];
		if (element.identifier == nullptr) {
			continue;
		}

		if (enum_type.is_tagged_union) {
			element.value = i;
			element.resolved = true;
			if (element.has_payload()) {
				BSParser::DataType::EnumCasePayload payload;
				for (const BSParser::EnumNode::PayloadField &field : element.payload_fields) {
					payload.field_names.push_back(field.identifier != nullptr ? field.identifier->name : StringName());
					payload.field_types.push_back(type_from_metatype(datatype_from_type_node(field.type)));
				}
				enum_type.enum_case_payloads[element.identifier->name] = payload;
			}
		} else if (element.custom_value != nullptr) {
			reduce_expression(element.custom_value);
			if (!element.custom_value->is_constant) {
				push_error(R"(Enum values must be constant.)", element.custom_value);
			} else if (element.custom_value->reduced_value.get_type() != Variant::INT) {
				push_error(R"(Enum values must be integers.)", element.custom_value);
			} else {
				element.value = element.custom_value->reduced_value;
				element.resolved = true;
			}
		} else {
			push_error(R"(Enum values must have an explicit integer value.)", element.identifier);
		}

		enum_type.enum_values[element.identifier->name] = element.value;
		dictionary[String(element.identifier->name)] = element.value;
	}

	p_enum->set_datatype(enum_type);
	p_enum->dictionary = dictionary;
	current_enum = previous_enum;
	current_enum_owner = previous_enum_owner;
	current_class = previous_class;
	return enum_type;
}

// Foundry complete_self_referential_enum_type @ c9d5e35 (`fs_analyzer_surface.cpp` ~920).
// A tagged union's payload field type may name the union itself, in which case it was captured
// while only the union's identity was published (see resolve_enum_values) and carries no cases.
// Re-read the declaration so a value typed from that field — a match bind, an element of a
// payload collection — sees the union's complete case set instead of the identity shell.
BSParser::DataType BSAnalyzer::complete_self_referential_enum_type(const BSParser::DataType &p_type) {
	BSParser::DataType completed = p_type;

	if (completed.kind == BSParser::DataType::ENUM && completed.is_tagged_union &&
			completed.enum_values.is_empty() && completed.class_type != nullptr) {
		const BSParser::ClassNode *owner = completed.class_type;
		const BSParser::EnumNode *declaration = nullptr;
		if (owner->is_enum_file) {
			declaration = owner->enum_file_decl;
		} else if (owner->has_member(completed.enum_type)) {
			const BSParser::ClassNode::Member member = owner->get_member(completed.enum_type);
			if (member.type == BSParser::ClassNode::Member::ENUM) {
				declaration = member.m_enum;
			}
		}

		if (declaration != nullptr) {
			const BSParser::DataType declared_type = declaration->get_datatype();
			if (declared_type.is_set() && declared_type.kind == BSParser::DataType::ENUM &&
					!declared_type.enum_values.is_empty()) {
				completed.enum_values = declared_type.enum_values;
				completed.enum_case_payloads = declared_type.enum_case_payloads;
				// The schema just re-read from the declaration names the declaration's own parameters,
				// while the shell carries the arguments the use site applied. Specializing here is what
				// keeps a recursive generic union concrete at every level instead of degrading a nested
				// value back to the open declaration.
				completed = specialize_enum_type(completed, declaration,
						enum_type_argument_bindings(declaration, completed.type_arguments));
			}
		}
	}

	// A payload field may nest the union anywhere a datatype can appear: a typed collection or
	// tuple (`Array[Chain]`), a callable signature (`Callable[[], Chain]`), a generic argument
	// (`Box[Chain]`), or a type-parameter bound. Descend into every such slot. `enum_case_payloads`
	// is deliberately excluded: a union's payload map names the union itself, so it has no finite
	// fixed point, and each level reaches this helper again when it is used to type a value.
	auto complete_each = [](Vector<BSParser::DataType> &r_types) {
		for (int i = 0; i < r_types.size(); i++) {
			r_types.write[i] = complete_self_referential_enum_type(r_types[i]);
		}
	};
	complete_each(completed.container_element_types);
	complete_each(completed.method_parameter_types);
	complete_each(completed.method_return_type);
	complete_each(completed.method_rest_parameter_type);
	complete_each(completed.type_parameter_bound);
	complete_each(completed.type_arguments);

	return completed;
}

HashMap<StringName, BSParser::DataType> BSAnalyzer::enum_type_argument_bindings(
		const BSParser::EnumNode *p_declaration, const Vector<BSParser::DataType> &p_arguments) {
	HashMap<StringName, BSParser::DataType> bindings;
	if (p_declaration == nullptr || p_declaration->type_parameters.size() != p_arguments.size()) {
		return bindings;
	}
	for (int i = 0; i < p_arguments.size(); i++) {
		const BSParser::TypeParameterNode *parameter = p_declaration->type_parameters[i];
		if (parameter != nullptr && parameter->identifier != nullptr) {
			bindings.insert(parameter->identifier->name, p_arguments[i]);
		}
	}
	return bindings;
}

BSParser::DataType BSAnalyzer::specialize_enum_type(const BSParser::DataType &p_type,
		const BSParser::EnumNode *p_declaration,
		const HashMap<StringName, BSParser::DataType> &p_bindings) {
	if (p_declaration == nullptr || p_bindings.is_empty() || p_type.enum_case_payloads.is_empty()) {
		return p_type;
	}

	BSParser::DataType result = p_type;
	for (KeyValue<StringName, BSParser::DataType::EnumCasePayload> &entry : result.enum_case_payloads) {
		for (int i = 0; i < entry.value.field_types.size(); i++) {
			entry.value.field_types.write[i] = BSParser::DataType::substitute(entry.value.field_types[i], p_bindings);
		}
	}
	return result;
}

BSParser::DataType BSAnalyzer::lookup_local_enum_meta_type(const StringName &p_name, BSParser::Node *p_source) {
	for (BSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
		if (!scope->has_member(p_name)) {
			continue;
		}
		const BSParser::ClassNode::Member &member = scope->get_member(p_name);
		if (member.type != BSParser::ClassNode::Member::ENUM || member.m_enum == nullptr) {
			continue;
		}
		resolve_class_member(scope, p_name, p_source);
		BSParser::DataType enum_meta = member.m_enum->get_datatype();
		if (enum_meta.is_set()) {
			return enum_meta;
		}
	}
	return BSParser::DataType();
}

void BSAnalyzer::mark_implicit_signal_usage(BSParser::CallNode *p_call, bool p_is_self) {
#ifdef DEBUG_ENABLED
	// Foundry @ c9d5e35: emit_signal / connect / disconnect / is_connected count as uses.
	if (!p_is_self || p_call == nullptr || parser == nullptr || current_class == nullptr || p_call->arguments.is_empty()) {
		return;
	}
	if (p_call->function_name != SNAME("emit_signal") && p_call->function_name != SNAME("connect") &&
			p_call->function_name != SNAME("disconnect") && p_call->function_name != SNAME("is_connected")) {
		return;
	}
	BSParser::ExpressionNode *signal_arg = p_call->arguments[0];
	if (signal_arg == nullptr || !signal_arg->is_constant) {
		return;
	}
	const StringName signal_name = signal_arg->reduced_value;
	if (!current_class->has_member(signal_name)) {
		return;
	}
	const BSParser::ClassNode::Member &member = current_class->get_member(signal_name);
	if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
		member.signal->usages++;
	}
#else
	(void)p_call;
	(void)p_is_self;
#endif
}

bool BSAnalyzer::has_member_name_conflict_in_script_class(const StringName &p_member_name, const BSParser::ClassNode *p_class, const BSParser::Node *p_member) const {
	if (p_class == nullptr || !p_class->members_indices.has(p_member_name)) {
		return false;
	}
	const BSParser::ClassNode::Member &member = p_class->members[p_class->members_indices[p_member_name]];
	if (member.type == BSParser::ClassNode::Member::VARIABLE || member.type == BSParser::ClassNode::Member::CONSTANT ||
			member.type == BSParser::ClassNode::Member::ENUM || member.type == BSParser::ClassNode::Member::ENUM_VALUE ||
			member.type == BSParser::ClassNode::Member::CLASS || member.type == BSParser::ClassNode::Member::SIGNAL) {
		return true;
	}
	return p_member != nullptr && p_member->type != BSParser::Node::FUNCTION && member.type == BSParser::ClassNode::Member::FUNCTION;
}

static bool _member_is_visible_outer_class_surface(const BSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case BSParser::ClassNode::Member::CONSTANT:
		case BSParser::ClassNode::Member::ENUM:
		case BSParser::ClassNode::Member::ENUM_VALUE:
		case BSParser::ClassNode::Member::CLASS:
		case BSParser::ClassNode::Member::TUPLE:
			return true;
		default:
			return false;
	}
}

bool BSAnalyzer::has_member_name_conflict_in_native_type(const StringName &p_member_name, const StringName &p_native_type) const {
	if (ClassDB::class_has_signal(p_native_type, p_member_name) || ClassDB::class_has_integer_constant(p_native_type, p_member_name)) {
		return true;
	}
	const TypedArray<Dictionary> properties = ClassDB::class_get_property_list(p_native_type, false);
	for (int i = 0; i < properties.size(); i++) {
		const Dictionary property = properties[i];
		if (StringName(property.get("name", String())) == p_member_name) {
			return true;
		}
	}
	return p_member_name == SNAME("script");
}

Error BSAnalyzer::check_native_member_name_conflict(const StringName &p_member_name, const BSParser::Node *p_member_node, const StringName &p_native_type) {
	if (has_member_name_conflict_in_native_type(p_member_name, p_native_type)) {
		push_error(vformat(R"*(Member "%s" redefined (original in native class '%s'))*", p_member_name, p_native_type), p_member_node);
		return ERR_PARSE_ERROR;
	}
	if (ClassDB::class_exists(p_member_name)) {
		push_error(vformat(R"*(The member "%s" shadows a native class.)*", p_member_name), p_member_node);
		return ERR_PARSE_ERROR;
	}
	if (BSParser::get_builtin_type(p_member_name) < Variant::VARIANT_MAX || p_member_name == SNAME("AsyncCallable")) {
		push_error(vformat(R"*(The member "%s" cannot have the same name as a builtin type.)*", p_member_name), p_member_node);
		return ERR_PARSE_ERROR;
	}
	if (p_member_name == BSParser::get_number_type_name()) {
		push_error(R"*(The member "Number" cannot have the same name as the compiler-provided type "Number".)*", p_member_node);
		return ERR_PARSE_ERROR;
	}
	return OK;
}

Error BSAnalyzer::check_outer_class_member_name_conflict(const BSParser::ClassNode *p_class, const StringName &p_member_name, const BSParser::Node *p_member_node) {
	if (p_class == nullptr || p_class->outer == nullptr) {
		return OK;
	}
	List<BSParser::ClassNode *> outer_scope_classes;
	get_class_node_current_scope_classes(p_class->outer, &outer_scope_classes, const_cast<BSParser::Node *>(p_member_node));
	for (BSParser::ClassNode *outer_class : outer_scope_classes) {
		if (outer_class == nullptr) {
			continue;
		}
		if (outer_class->identifier != nullptr && outer_class->identifier->name == p_member_name) {
			push_error(vformat(R"*(The member "%s" already exists in outer class %s.)*", p_member_name, bs_class_or_trait_diagnostic_name(outer_class)), p_member_node);
			return ERR_PARSE_ERROR;
		}
		if (!outer_class->members_indices.has(p_member_name)) {
			continue;
		}
		const BSParser::ClassNode::Member &outer_member = outer_class->members[outer_class->members_indices[p_member_name]];
		if (_member_is_visible_outer_class_surface(outer_member) && has_member_name_conflict_in_script_class(p_member_name, outer_class, p_member_node)) {
			push_error(vformat(R"*(The member "%s" already exists in outer class %s.)*", p_member_name, bs_class_or_trait_diagnostic_name(outer_class)), p_member_node);
			return ERR_PARSE_ERROR;
		}
	}
	return OK;
}

Error BSAnalyzer::check_class_member_name_conflict(const BSParser::ClassNode *p_class, const StringName &p_member_name, const BSParser::Node *p_member_node) {
	if (p_class == nullptr) {
		return OK;
	}
	const BSParser::DataType *current_type = &p_class->base_type;
	HashSet<const BSParser::ClassNode *> visited;
	while (current_type != nullptr && current_type->kind == BSParser::DataType::CLASS && current_type->class_type != nullptr) {
		BSParser::ClassNode *parent = current_type->class_type;
		// BaristaScript's cross-file fail-stop can retain the already diagnosed CLASS edge while
		// unwinding a cycle. Keep this surface walk bounded exactly like the other inheritance walks.
		if (visited.has(parent)) {
			break;
		}
		visited.insert(parent);
		if (has_member_name_conflict_in_script_class(p_member_name, parent, p_member_node)) {
			const String parent_name = parent->identifier != nullptr ? String(parent->identifier->name) : parent->fqcn;
			push_error(vformat(R"*(The member "%s" already exists in parent class %s.)*", p_member_name, parent_name), p_member_node);
			return ERR_PARSE_ERROR;
		}
		current_type = &parent->base_type;
	}
	if (current_type != nullptr && current_type->kind == BSParser::DataType::NATIVE && current_type->native_type != StringName()) {
		const Error err = check_native_member_name_conflict(p_member_name, p_member_node, current_type->native_type);
		if (err != OK) {
			return err;
		}
	}
	return check_outer_class_member_name_conflict(p_class, p_member_name, p_member_node);
}

void BSAnalyzer::get_class_node_current_scope_classes(BSParser::ClassNode *p_node, List<BSParser::ClassNode *> *p_list, BSParser::Node *p_source) {
	// Foundry get_class_node_current_scope_classes @ c9d5e35: base CLASS chain before outer.
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(p_list);
	if (p_list->find(p_node) != nullptr) {
		return;
	}
	p_list->push_back(p_node);

	auto resolve_for_scope_traverse = [&](BSParser::ClassNode *p_scope_class) {
		if (p_scope_class == nullptr || p_scope_class->base_type.is_resolving()) {
			return;
		}
		if (!p_scope_class->base_type.is_set()) {
			resolve_class_inheritance(p_scope_class);
		}
		(void)p_source;
	};

	if (p_node->base_type.class_type != nullptr) {
		resolve_for_scope_traverse(p_node->base_type.class_type);
		get_class_node_current_scope_classes(p_node->base_type.class_type, p_list, p_source);
	}
	if (p_node->outer != nullptr) {
		resolve_for_scope_traverse(p_node->outer);
		get_class_node_current_scope_classes(p_node->outer, p_list, p_source);
	}
}

// The declaration fallback belongs only to lookups starting at this witness target.
void BSAnalyzer::get_effective_scope_classes(BSParser::ClassNode *p_node, List<BSParser::ClassNode *> *p_list,
		const BSParser::Node *p_source, HashSet<BSParser::ClassNode *> *r_declarations) {
	if (p_node == nullptr)
		return;
	get_class_node_current_scope_classes(p_node, p_list, const_cast<BSParser::Node *>(p_source));
	if (witness_declaration_scope == nullptr || p_node != witness_target_class)
		return;
	List<BSParser::ClassNode *> declarations;
	get_class_node_current_scope_classes(witness_declaration_scope, &declarations, const_cast<BSParser::Node *>(p_source));
	for (BSParser::ClassNode *scope : declarations) {
		if (p_list->find(scope) != nullptr)
			continue;
		p_list->push_back(scope);
		if (r_declarations != nullptr)
			r_declarations->insert(scope);
	}
}

bool BSAnalyzer::is_type_bearing_member(const BSParser::ClassNode::Member &p_member) const {
	switch (p_member.type) {
		case BSParser::ClassNode::Member::CLASS:
		case BSParser::ClassNode::Member::ENUM:
		case BSParser::ClassNode::Member::TUPLE:
		case BSParser::ClassNode::Member::TYPE_ALIAS:
			return true;
		case BSParser::ClassNode::Member::CONSTANT:
			// #140 semantic class/preload handles carry their authoritative metatype without
			// materializing a runtime Script object.
			return p_member.get_datatype().is_meta_type;
		default:
			return false;
	}
}

BSParser::ClassNode *BSAnalyzer::find_witness_declaration_type(const StringName &p_name, const BSParser::Node *p_source) {
	if (current_class != witness_target_class || witness_declaration_scope == nullptr || witness_target_scope_declares_name(p_name, p_source))
		return nullptr;
	List<BSParser::ClassNode *> scopes;
	HashSet<BSParser::ClassNode *> declarations;
	get_effective_scope_classes(current_class, &scopes, p_source, &declarations);
	for (BSParser::ClassNode *scope : scopes) {
		if (!declarations.has(scope))
			continue;
		if (scope->identifier != nullptr && scope->identifier->name == p_name)
			return scope;
		if (!scope->has_member(p_name))
			continue;
		resolve_class_member(scope, p_name, p_source);
		if (is_type_bearing_member(scope->get_member(p_name)))
			return scope;
	}
	return nullptr;
}

bool BSAnalyzer::reduce_identifier_from_witness_declaration_scope(BSParser::IdentifierNode *p_identifier) {
	BSParser::ClassNode *scope = find_witness_declaration_type(p_identifier->name, p_identifier);
	if (scope == nullptr)
		return false;
	if (scope->identifier != nullptr && scope->identifier->name == p_identifier->name) {
		BSParser::DataType type = scope->get_datatype();
		type.is_meta_type = true;
		if (!scope->is_trait) {
			p_identifier->is_constant = true;
			p_identifier->is_unmaterialized_constant = true;
		}
		p_identifier->set_datatype(type);
		p_identifier->source = BSParser::IdentifierNode::MEMBER_CLASS;
	} else if (scope->get_member(p_identifier->name).type == BSParser::ClassNode::Member::TYPE_ALIAS) {
		push_error(vformat(R"(Type alias "%s" can only be used in a type position. It declares no value, so it cannot be called, constructed, or read.)", p_identifier->name), p_identifier);
		BSParser::DataType type;
		type.kind = BSParser::DataType::VARIANT;
		p_identifier->set_datatype(type);
	} else if (!try_bind_identifier_member(p_identifier, scope, false)) {
		return false;
	}
	p_identifier->resolved_from_conformance_declaration_scope = true;
	return true;
}

// Foundry surface:1302-1361: only the compiler's flattened surface is reachable.
// Abstract receivers additionally expose requirements; lexical outers are never donors.
BSParser::ClassNode *BSAnalyzer::find_trait_member_in_inheritance_chain(BSParser::ClassNode *p_receiver,
		const StringName &p_name, const BSParser::Node *p_source) {
	HashSet<BSParser::ClassNode *> seen_owners;
	HashSet<BSParser::ClassNode *> seen_traits;
	const bool allow_abstract = p_receiver != nullptr && (p_receiver->is_trait || p_receiver->is_abstract);
	for (BSParser::ClassNode *owner = p_receiver; owner != nullptr; owner = owner->base_type.class_type) {
		if (seen_owners.has(owner)) {
			break;
		}
		seen_owners.insert(owner);
		if (owner->resolving_trait_uses) {
			continue;
		}
		resolve_used_traits(owner);
		if (!owner->resolved_trait_uses || owner->failed_trait_uses) {
			continue;
		}
		for (BSParser::ClassNode *trait : owner->resolved_traits) {
			if (trait == nullptr || seen_traits.has(trait)) {
				continue;
			}
			seen_traits.insert(trait);
			if (!trait->has_member(p_name)) {
				continue;
			}
			const auto &member = trait->get_member(p_name);
			switch (member.type) {
				case BSParser::ClassNode::Member::VARIABLE:
				case BSParser::ClassNode::Member::CONSTANT:
				case BSParser::ClassNode::Member::ENUM:
				case BSParser::ClassNode::Member::ENUM_VALUE:
				case BSParser::ClassNode::Member::SIGNAL:
					break;
				case BSParser::ClassNode::Member::FUNCTION:
					if (member.function == nullptr || (!allow_abstract && member.function->is_abstract)) {
						continue;
					}
					break;
				default:
					continue;
			}
			resolve_class_member(trait, p_name, p_source);
			return trait;
		}
	}
	return nullptr;
}

BSParser::ClassNode *BSAnalyzer::find_member_in_class_or_trait_chain(BSParser::ClassNode *p_receiver,
		const StringName &p_name, const BSParser::Node *p_source) {
	HashSet<BSParser::ClassNode *> visited;
	for (BSParser::ClassNode *owner = p_receiver; owner != nullptr; owner = owner->base_type.class_type) {
		if (visited.has(owner)) {
			break;
		}
		visited.insert(owner);
		if (owner->has_member(p_name)) {
			return owner;
		}
	}
	return find_trait_member_in_inheritance_chain(p_receiver, p_name, p_source);
}

void BSAnalyzer::resolve_class_member(BSParser::ClassNode *p_class, const StringName &p_name, const BSParser::Node *p_source) {
	ERR_FAIL_COND(p_class == nullptr || !p_class->has_member(p_name));
	resolve_class_member(p_class, p_class->members_indices[p_name], p_source);
}

void BSAnalyzer::resolve_class_member(BSParser::ClassNode *p_class, int p_index, const BSParser::Node *p_source) {
	// Foundry resolve_class_member @ c9d5e35 (`fs_analyzer_surface.cpp` ~1665): lazy member datatype
	// resolution with cyclic RESOLVING fail-stop. Hard fork FS*→BS*; external path uses
	// retained owner delegation with ForeignAnalyzerVisibilityScope, owner member
	// failure recording, and dependent_resolution_failure_replays dedupe. Class-phase
	// INTERFACE/BODY foreign recording/replay is in analyze_class_interface / analyze_class_body.
	ERR_FAIL_NULL(p_class);
	ERR_FAIL_INDEX(p_index, p_class->members.size());
	ERR_FAIL_NULL(parser);

	BSParser::ClassNode::Member &member = p_class->members.write[p_index];
	if (p_source == nullptr && parser->has_class(p_class)) {
		p_source = member.get_source_node();
	}

	const bool owns_class = parser->has_class(p_class) || p_class->is_native_conformance_shim || p_class->is_builtin_conformance_shim;
	auto push_external_member_failure = [&]() {
		if (dependent_resolution_failure_replays.record_member(p_class, p_index)) {
			push_error(vformat(R"(Could not resolve external class member "%s".)", member.get_name()), p_source);
		}
	};

	if (member.get_datatype().is_resolving()) {
		push_error(vformat(R"(Could not resolve member "%s": Cyclic reference.)", member.get_name()), p_source);
		return;
	}

	Ref<BSParserRef> parser_ref;
	if (!owns_class) {
		parser_ref = ensure_external_parser(p_class, "While resolving external class member", p_source);
		if (parser_ref.is_null()) {
			return;
		}
	}
	if (member.get_datatype().is_set()) {
		if (!owns_class && parser_ref->get_analyzer()->owner_resolution_failures.has_member(p_class, p_index)) {
			push_external_member_failure();
		}
		return;
	}
	if (!owns_class) {
		BSAnalyzer *other_analyzer = parser_ref->get_analyzer();
		BSParser *other_parser = parser_ref->get_parser();
		const int error_count = other_parser->get_errors().size();
		ForeignAnalyzerVisibilityScope visibility_scope(other_analyzer);
		other_analyzer->resolve_class_member(p_class, p_index);
		if (other_parser->get_errors().size() > error_count ||
				other_analyzer->owner_resolution_failures.has_member(p_class, p_index)) {
			push_external_member_failure();
		}
		return;
	}
	if (!p_class->base_type.is_resolving()) {
		resolve_class_inheritance(p_class);
	}

	BSParser::ClassNode *previous_class = current_class;
	current_class = p_class;

	BSParser::DataType resolving_datatype;
	resolving_datatype.kind = BSParser::DataType::RESOLVING;
	const int member_error_count = parser->get_errors().size();

	switch (member.type) {
		case BSParser::ClassNode::Member::VARIABLE: {
			if (member.variable == nullptr) {
				break;
			}
			member.variable->set_datatype(resolving_datatype);
			auto is_builtin_export = [](const BSParser::AnnotationNode *annotation) {
				return annotation && annotation->info && !annotation->is_custom && String(annotation->name).begins_with("@export");
			};

			for (BSParser::AnnotationNode *annotation : member.variable->annotations) {
				if (annotation != nullptr && annotation->name != SNAME("@warning_ignore")) {
					resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_VARIABLE);
					if (!is_builtin_export(annotation))
						annotation->apply(parser, member.variable, p_class);
				}
			}

			BSParser::DataType type;
			const bool has_specified_type = member.variable->datatype_specifier != nullptr;
			if (has_specified_type) {
				type = datatype_from_type_node(member.variable->datatype_specifier);
			}

			if (member.variable->initializer != nullptr) {
				// S6: Foundry surface:1758-1820 scopes static member initializer context.
				const BSParser::VariableNode *previous_initializer = get_node_initializer;
				get_node_initializer = member.variable;
				reduce_expression(member.variable->initializer);
				get_node_initializer = previous_initializer;
				qualify_contextual_enum_case_consumer(member.variable->initializer, type);
				mark_coroutine_handle_capture(member.variable->initializer, type);
				const bool constant_type_ok = update_constant_expression_type(member.variable->initializer, type, "assign");
				const BSParser::DataType initializer_type = member.variable->initializer->get_datatype();

				check_assignable_inference(member.variable, "variable");

				if (!has_specified_type) {
					type = initializer_type;
					if (!type.is_set() || (type.is_hard_type() && type.kind == BSParser::DataType::BUILTIN && type.builtin_type == Variant::NIL)) {
						type = BSParser::DataType();
						type.kind = BSParser::DataType::VARIANT;
					}
					if (member.variable->infer_datatype) {
						type.type_source = BSParser::DataType::ANNOTATED_INFERRED;
					} else {
						type.type_source = BSParser::DataType::INFERRED;
					}
				} else if (constant_type_ok && type.is_set() && !type.is_variant() && initializer_type.is_set()) {
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (BSAnalyzer::has_materialized_constant_value(member.variable->initializer)) {
						options.constant_source_value = &member.variable->initializer->reduced_value;
					}
					if (!BSTypeCompatibility::check(type, initializer_type, options).compatible) {
						push_error(vformat(R"(Cannot assign a value of type "%s" to a variable of type "%s".)",
										   initializer_type.to_string(), type.to_string()) +
										BSParser::DataType::same_rendered_name_clause(initializer_type, "value", type, "specified type"),
								member.variable->initializer);
					}
				}
			}

			if (!type.is_set()) {
				type.kind = BSParser::DataType::VARIANT;
				type.type_source = BSParser::DataType::UNDETECTED;
			}
			member.variable->set_datatype(type);
			// The existing export callback consumes the completed datatype (parser export_annotations).
			for (BSParser::AnnotationNode *annotation : member.variable->annotations) {
				if (is_builtin_export(annotation))
					annotation->apply(parser, member.variable, p_class);
			}
		} break;
		case BSParser::ClassNode::Member::CONSTANT: {
			if (member.constant == nullptr) {
				break;
			}
			member.constant->set_datatype(resolving_datatype);

			for (BSParser::AnnotationNode *annotation : member.constant->annotations) {
				if (annotation != nullptr) {
					resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_CONSTANT);
					annotation->apply(parser, member.constant, p_class);
				}
			}

			BSParser::DataType type;
			const bool has_specified_type = member.constant->datatype_specifier != nullptr;
			if (has_specified_type) {
				type = datatype_from_type_node(member.constant->datatype_specifier);
			}

			if (member.constant->initializer != nullptr) {
				reduce_expression(member.constant->initializer);
				qualify_contextual_enum_case_consumer(member.constant->initializer, type);
				mark_coroutine_handle_capture(member.constant->initializer, type);
				materialize_constant_initializer(member.constant);
				const bool constant_type_ok = update_constant_expression_type(member.constant->initializer, type, "assign");
				check_assignable_inference(member.constant, "constant");
				const BSParser::DataType initializer_type = member.constant->initializer->get_datatype();

				if (!has_specified_type) {
					if (!initializer_type.is_set()) {
						type.kind = BSParser::DataType::VARIANT;
						type.type_source = BSParser::DataType::UNDETECTED;
					} else {
						type = initializer_type;
						type.type_source = BSParser::DataType::ANNOTATED_INFERRED;
						type.is_constant = true;
					}
				} else if (constant_type_ok && type.is_set() && !type.is_variant() && initializer_type.is_set()) {
					BSTypeCompatibility::Options options;
					options.allow_implicit_conversion = true;
					options.strict_dynamic = strict_dynamic_checks;
					options.strict_null = strict_null_checks;
					if (BSAnalyzer::has_materialized_constant_value(member.constant->initializer)) {
						options.constant_source_value = &member.constant->initializer->reduced_value;
					}
					if (!BSTypeCompatibility::check(type, initializer_type, options).compatible) {
						push_error(vformat(R"(Cannot assign a value of type "%s" to a constant of type "%s".)",
										   initializer_type.to_string(), type.to_string()) +
										BSParser::DataType::same_rendered_name_clause(initializer_type, "value", type, "specified type"),
								member.constant->initializer);
					}
				}
			}

			if (!type.is_set()) {
				type.kind = BSParser::DataType::VARIANT;
				type.type_source = BSParser::DataType::UNDETECTED;
			}
			type.is_constant = true;
			member.constant->set_datatype(type);
		} break;
		case BSParser::ClassNode::Member::SIGNAL: {
			if (member.signal == nullptr) {
				break;
			}
			member.signal->set_datatype(resolving_datatype);

			MethodInfo mi = MethodInfo(member.signal->identifier != nullptr ? member.signal->identifier->name : StringName());
			BSParser::DataType signal_type;
			signal_type.type_source = BSParser::DataType::ANNOTATED_EXPLICIT;
			signal_type.kind = BSParser::DataType::BUILTIN;
			signal_type.builtin_type = Variant::SIGNAL;
			signal_type.is_constant = true;
			signal_type.has_method_signature = true;
			signal_type.has_explicit_method_signature = true;
			for (int j = 0; j < member.signal->parameters.size(); j++) {
				BSParser::ParameterNode *param = member.signal->parameters[j];
				if (param == nullptr) {
					continue;
				}
				if (param->datatype_specifier != nullptr) {
					param->set_datatype(datatype_from_type_node(param->datatype_specifier));
				}
				const BSParser::DataType param_type = param->get_datatype();
				signal_type.method_parameter_types.push_back(param_type);
				if (param->identifier != nullptr) {
					mi.arguments.push_back(param_type.to_property_info(param->identifier->name));
				}
			}
			signal_type.method_info = mi;
			member.signal->method_info = mi;
			member.signal->set_datatype(signal_type);

			for (BSParser::AnnotationNode *annotation : member.signal->annotations) {
				if (annotation != nullptr) {
					resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_SIGNAL);
					annotation->apply(parser, member.signal, p_class);
				}
			}
		} break;
		case BSParser::ClassNode::Member::ENUM: {
			if (member.m_enum == nullptr || member.m_enum->identifier == nullptr) {
				break;
			}
			member.m_enum->set_datatype(resolving_datatype);
			const String script_path = parser->script_path;
			BSParser::DataType enum_shell = make_class_enum_type(member.m_enum->identifier->name, p_class, script_path, true);
			resolve_enum_values(member.m_enum, enum_shell, p_class);
		} break;
		case BSParser::ClassNode::Member::FUNCTION: {
			if (member.function == nullptr) {
				break;
			}
			for (BSParser::AnnotationNode *annotation : member.function->annotations) {
				if (annotation != nullptr) {
					resolve_annotation(annotation, BSParser::AnnotationDeclarationNode::TARGET_METHOD);
					annotation->apply(parser, member.function, p_class);
				}
			}
			resolve_function_signature_in_class(member.function, p_class);
			if (!member.function->type_parameters.is_empty()) {
				push_error("Generic function specialization is not available until M5.", member.function);
			}
		} break;
		case BSParser::ClassNode::Member::CLASS: {
			if (member.m_class == nullptr) {
				break;
			}
			if (!member.m_class->base_type.is_resolving()) {
				resolve_class_inheritance(member.m_class);
			}
		} break;
		case BSParser::ClassNode::Member::TUPLE: {
			if (member.m_tuple == nullptr || member.m_tuple->identifier == nullptr) {
				break;
			}
			check_class_member_name_conflict(p_class, member.m_tuple->identifier->name, member.m_tuple);
			member.m_tuple->set_datatype(resolving_datatype);

			Vector<BSParser::DataType> element_types;
			Vector<StringName> field_names;
			for (const BSParser::TupleNode::Field &field : member.m_tuple->fields) {
				element_types.push_back(type_from_metatype(datatype_from_type_node(field.type)));
				field_names.push_back(field.identifier != nullptr ? field.identifier->name : StringName());
			}
			member.m_tuple->set_datatype(make_tuple_type(member.m_tuple->identifier->name, p_class->fqcn,
					parser->script_path, element_types, field_names, true));
		} break;
		case BSParser::ClassNode::Member::ENUM_VALUE: {
			// Foundry surface:1928: unnamed enum values are ordinary flattened members.
			member.enum_value.identifier->set_datatype(resolving_datatype);
			if (member.enum_value.custom_value != nullptr) {
				check_class_member_name_conflict(p_class, member.enum_value.identifier->name, member.enum_value.custom_value);
				BSParser::EnumNode *previous_enum = current_enum;
				current_enum = member.enum_value.parent_enum;
				reduce_expression(member.enum_value.custom_value);
				current_enum = previous_enum;
				if (!member.enum_value.custom_value->is_constant) {
					push_error(R"(Enum values must be constant.)", member.enum_value.custom_value);
				} else if (!has_materialized_constant_value(member.enum_value.custom_value) || member.enum_value.custom_value->reduced_value.get_type() != Variant::INT) {
					push_error(R"(Enum values must be integers.)", member.enum_value.custom_value);
				} else {
					member.enum_value.value = member.enum_value.custom_value->reduced_value;
					member.enum_value.resolved = true;
				}
			} else {
				check_class_member_name_conflict(p_class, member.enum_value.identifier->name, member.enum_value.parent_enum);
				push_error(R"(Enum values must have an explicit integer value.)", member.enum_value.identifier);
			}
			if (member.enum_value.parent_enum != nullptr) {
				member.enum_value.parent_enum->values.set(member.enum_value.index, member.enum_value);
			}
			member.enum_value.identifier->set_datatype(make_class_enum_type("<anonymous enum>", p_class, parser->script_path, false));
		} break;
		case BSParser::ClassNode::Member::GROUP:
		case BSParser::ClassNode::Member::UNDEFINED:
			break;
		case BSParser::ClassNode::Member::TYPE_ALIAS: {
			if (member.type_alias == nullptr || member.type_alias->identifier == nullptr) {
				break;
			}
			const StringName alias_name = member.type_alias->identifier->name;
			if (BSParser::is_builtin_data_type(alias_name) || alias_name == SNAME("AsyncCallable")) {
				push_error(vformat(R"(Type alias "%s" hides a built-in type.)", alias_name), member.type_alias->identifier);
			} else if (alias_name == BSParser::get_number_type_name()) {
				push_error(R"(Type alias "Number" hides the compiler-provided type "Number".)", member.type_alias->identifier);
			} else if (ClassDB::class_exists(alias_name)) {
				push_error(vformat(R"(Type alias "%s" hides a native class.)", alias_name), member.type_alias->identifier);
			}
			resolve_type_alias(member.type_alias);
		} break;
	}

	if (parser->get_errors().size() > member_error_count) {
		owner_resolution_failures.record_member(p_class, p_index, member_error_count);
	}

	current_class = previous_class;
}

bool BSAnalyzer::try_bind_identifier_member(BSParser::IdentifierNode *p_identifier, BSParser::ClassNode *p_class, bool p_mark_inherited) {
	if (p_identifier == nullptr || p_class == nullptr || !p_class->has_member(p_identifier->name)) {
		return false;
	}
	// Foundry @ c9d5e35: resolve before reading member datatypes so later-declared consts /
	// inferred vars / cyclic refs are not silently Variant.
	resolve_class_member(p_class, p_identifier->name, p_identifier);
	const BSParser::ClassNode::Member member = p_class->get_member(p_identifier->name);
	if (member.type == BSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
		BSParser::DataType class_type = member.m_class->get_datatype();
		class_type.is_meta_type = true;
		p_identifier->source = BSParser::IdentifierNode::MEMBER_CLASS;
		if (!member.m_class->is_trait) {
			p_identifier->is_constant = true;
			p_identifier->is_unmaterialized_constant = true;
		}
		p_identifier->set_datatype(class_type);
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::TUPLE && member.m_tuple != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_CLASS;
		p_identifier->set_datatype(member.m_tuple->get_datatype());
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::VARIABLE && member.variable != nullptr) {
		if (p_mark_inherited && !member.variable->is_static) {
			p_identifier->source = BSParser::IdentifierNode::INHERITED_VARIABLE;
		} else {
			p_identifier->source = member.variable->is_static ? BSParser::IdentifierNode::STATIC_VARIABLE : BSParser::IdentifierNode::MEMBER_VARIABLE;
		}
		p_identifier->variable_source = member.variable;
		member.variable->usages++;
		p_identifier->set_datatype(member.variable->get_datatype());
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::CONSTANT && member.constant != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
		p_identifier->constant_source = member.constant;
		member.constant->usages++;
		p_identifier->set_datatype(member.constant->get_datatype());
		if (member.constant->initializer != nullptr && member.constant->initializer->is_constant) {
			p_identifier->is_constant = true;
			p_identifier->reduced_value = member.constant->initializer->reduced_value;
			p_identifier->is_unmaterialized_constant = !has_materialized_constant_value(member.constant->initializer);
		}
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::SIGNAL && member.signal != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_SIGNAL;
		p_identifier->signal_source = member.signal;
		member.signal->usages++;
		const BSParser::DataType owner_type = current_class != nullptr && current_class->get_datatype().is_set()
				? current_class->get_datatype()
				: p_class->get_datatype();
		p_identifier->set_datatype(call_site_validation.explicit_signal_type_from_node(member.signal, owner_type, p_class));
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::FUNCTION && member.function != nullptr) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_FUNCTION;
		p_identifier->function_source = member.function;
		p_identifier->function_source_is_static = member.function->is_static;
		p_identifier->set_datatype(call_site_validation.callable_type_from_function(member.function));
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::ENUM_VALUE) {
		p_identifier->source = BSParser::IdentifierNode::MEMBER_CONSTANT;
		p_identifier->set_datatype(member.get_datatype());
		p_identifier->is_constant = true;
		p_identifier->reduced_value = member.enum_value.value;
		return true;
	}
	if (member.type == BSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
		BSParser::DataType enum_meta = member.m_enum->get_datatype();
		if (!enum_meta.is_set() && !enum_meta.is_resolving()) {
			const String script_path = parser != nullptr ? parser->script_path : String();
			enum_meta = resolve_enum_values(member.m_enum, make_class_enum_type(p_identifier->name, p_class, script_path, true), p_class);
		}
		if (enum_meta.is_set() || enum_meta.is_resolving()) {
			p_identifier->set_datatype(enum_meta);
			p_identifier->is_constant = true;
			return true;
		}
	}
	return false;
}

bool BSAnalyzer::try_bind_identifier_member_in_inheritance(BSParser::IdentifierNode *p_identifier, BSParser::ClassNode *p_class, bool p_is_lexical_outer) {
	if (p_identifier == nullptr || p_class == nullptr) {
		return false;
	}
	bool first = true;
	// Soft mutual path-extends may install CLASS loops for registration; stop identity revisits.
	HashSet<const BSParser::ClassNode *> visited;
	for (BSParser::ClassNode *lookup = p_class; lookup != nullptr; lookup = lookup->base_type.class_type) {
		if (visited.has(lookup)) {
			break;
		}
		visited.insert(lookup);
		// Foundry fs_analyzer.cpp:12018,12146-12172: lexical outers do not supply
		// receiver variables/functions/signals. Reuse the same visible-outer policy as conflicts.
		const bool visible = !p_is_lexical_outer || (lookup->has_member(p_identifier->name) && _member_is_visible_outer_class_surface(lookup->get_member(p_identifier->name)));
		if (visible && try_bind_identifier_member(p_identifier, lookup, !first)) {
			return true;
		}
		first = false;
	}
	if (p_is_lexical_outer) {
		return false;
	}
	const StringName native = p_class->base_type.native_type;
	if (native != StringName()) {
		const TypedArray<Dictionary> properties = ClassDB::class_get_property_list(native, false);
		for (int i = 0; i < properties.size(); i++) {
			const Dictionary property = properties[i];
			if (StringName(property.get("name", String())) == p_identifier->name) {
				p_identifier->set_datatype(type_from_property(PropertyInfo::from_dict(property)));
				p_identifier->source = BSParser::IdentifierNode::INHERITED_VARIABLE;
				return true;
			}
		}
		MethodInfo info;
		if (BSNativeDB::get_method_info(native, p_identifier->name, &info)) {
			p_identifier->set_datatype(call_site_validation.explicit_callable_type_from_info(info));
			p_identifier->source = BSParser::IdentifierNode::INHERITED_VARIABLE;
			return true;
		}
		if (ClassDB::class_has_signal(native, p_identifier->name) && BSNativeDB::get_signal(native, p_identifier->name, &info)) {
			p_identifier->set_datatype(call_site_validation.explicit_signal_type_from_info(info));
			p_identifier->source = BSParser::IdentifierNode::INHERITED_VARIABLE;
			return true;
		}
		if (ClassDB::class_has_integer_constant(native, p_identifier->name)) {
			p_identifier->is_constant = true;
			p_identifier->reduced_value = ClassDB::class_get_integer_constant(native, p_identifier->name);
			p_identifier->set_datatype(type_from_variant(p_identifier->reduced_value));
			return true;
		}
	}
	return false;
}

} // namespace barista_script
