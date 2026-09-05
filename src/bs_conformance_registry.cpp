/**************************************************************************/
/*  bs_conformance_registry.cpp                                           */
/*                                                                        */
/*  Hard fork of Foundry fs_conformance_registry.cpp @ c9d5e35. Visibility*/
/*  stack + declaration store with atomic try_replace_file_conformances  */
/*  + find_witness_location / find_hidden_witness_declaration /           */
/*  get_witness_source (method-name keys) + WITNESS_COLLISION arbitration*/
/*  + RecordedTypeArgument / ClassTraitBinding chain coherence against   */
/*  uses bindings + p_loaded_files load-graph licensing +                */
/*  declaration-side recorded trait-argument queries.                    */
/*  Runtime Function* witnesses remain residual under #60.               */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_conformance_registry.h"

#include "bs_type.h"

#include <cstring>

namespace barista_script {

BSConformanceRegistry *BSConformanceRegistry::singleton = nullptr;
thread_local const BSConformanceRegistry::Visibility *BSConformanceRegistry::active_visibility = nullptr;
thread_local bool BSConformanceRegistry::has_in_flight_source_file = false;
thread_local char BSConformanceRegistry::in_flight_source_file[1024] = {};

namespace {

// Mirrors Variant::MAX_RECURSION_DEPTH (1024). godot-cpp does not expose the enumerator.
static constexpr int TYPE_WALK_MAX_DEPTH = 1024;

// Drop the script-path alias for an inner-class target: that path is shared with siblings and the
// root class, so keeping it would make membership lookups cross class boundaries.
Vector<String> _identifying_target_keys(const Vector<String> &p_target_keys, const String &p_target_script_path,
		bool p_target_is_root_class) {
	if (p_target_is_root_class || p_target_script_path.is_empty() || !p_target_keys.has(p_target_script_path)) {
		return p_target_keys;
	}
	Vector<String> identifying;
	for (const String &target_key : p_target_keys) {
		if (target_key != p_target_script_path) {
			identifying.push_back(target_key);
		}
	}
	return identifying;
}

static bool _is_native_target(const BSConformanceRegistry::Conformance &p_conformance) {
	return p_conformance.target_script_path.is_empty() && ClassDB::class_exists(p_conformance.target_fqcn);
}

static bool _native_classes_are_on_one_chain(const StringName &p_class, const StringName &p_other) {
	return p_class != p_other &&
			(ClassDB::is_parent_class(p_class, p_other) || ClassDB::is_parent_class(p_other, p_class));
}

static bool _native_ancestry_answers_for(const StringName &p_terminal, const StringName &p_declared_on) {
	if (p_terminal == StringName() || p_declared_on == StringName()) {
		return false;
	}
	return p_terminal == p_declared_on || ClassDB::is_parent_class(p_terminal, p_declared_on);
}

static bool _script_classes_are_on_one_chain(const BSConformanceRegistry::Conformance &p_conformance,
		const BSConformanceRegistry::Conformance &p_other) {
	if (p_conformance.target_fqcn.is_empty() || p_other.target_fqcn.is_empty() ||
			p_conformance.target_fqcn == p_other.target_fqcn) {
		return false;
	}
	return p_conformance.target_script_ancestor_fqcns.has(p_other.target_fqcn) ||
			p_other.target_script_ancestor_fqcns.has(p_conformance.target_fqcn);
}

static bool _conformance_answers_for_binding(const BSConformanceRegistry::Conformance &p_conformance,
		const BSConformanceRegistry::ClassTraitBinding &p_binding) {
	if (_is_native_target(p_conformance)) {
		return _native_ancestry_answers_for(p_binding.target_native_base, StringName(p_conformance.target_fqcn));
	}
	if (p_conformance.target_fqcn.is_empty() || p_binding.target_fqcn.is_empty() ||
			p_conformance.target_fqcn == p_binding.target_fqcn) {
		return false;
	}
	return p_binding.target_script_ancestor_fqcns.has(p_conformance.target_fqcn) ||
			p_conformance.target_script_ancestor_fqcns.has(p_binding.target_fqcn);
}

static BSConformanceRegistry::RecordedTypeArgument _reduce_type_argument(const BSParser::DataType &p_type, int p_depth) {
	using RecordedTypeArgument = BSConformanceRegistry::RecordedTypeArgument;
	RecordedTypeArgument recorded;
	if (unlikely(p_depth > TYPE_WALK_MAX_DEPTH)) {
		return recorded;
	}
	if (!p_type.is_set() || p_type.is_meta_type || p_type.is_type_handle_annotation) {
		return recorded;
	}

	switch (p_type.kind) {
		case BSParser::DataType::BUILTIN: {
			if (p_type.has_method_signature) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::BUILTIN;
			recorded.builtin_type = p_type.builtin_type;
		} break;
		case BSParser::DataType::NATIVE: {
			if (p_type.native_type == StringName()) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::NATIVE_CLASS;
			recorded.native_class = p_type.native_type;
		} break;
		case BSParser::DataType::CLASS: {
			if (p_type.class_type == nullptr) {
				return recorded;
			}
			const String global_name = String(p_type.class_type->get_global_name());
			if (p_type.class_type->fqcn.is_empty() && global_name.is_empty()) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::SCRIPT_CLASS;
			recorded.script_fqcn = p_type.class_type->fqcn;
			recorded.script_global_name = global_name;
		} break;
		case BSParser::DataType::SCRIPT: {
			const String global_name = p_type.script_type.is_valid()
					? String(p_type.script_type->get_global_name())
					: String();
			if (!p_type.script_type.is_valid() && p_type.script_path.is_empty()) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::SCRIPT_CLASS;
			recorded.script_fqcn = p_type.script_path;
			recorded.script_global_name = global_name;
			if (recorded.script_fqcn.is_empty() && recorded.script_global_name.is_empty()) {
				return RecordedTypeArgument();
			}
		} break;
		default:
			return recorded;
	}

	recorded.is_nullable = p_type.is_nullable;
	recorded.type_arguments.resize(p_type.type_arguments.size());
	for (int i = 0; i < p_type.type_arguments.size(); i++) {
		recorded.type_arguments.write[i] = _reduce_type_argument(p_type.type_arguments[i], p_depth + 1);
	}
	recorded.container_element_types.resize(p_type.container_element_types.size());
	for (int i = 0; i < p_type.container_element_types.size(); i++) {
		recorded.container_element_types.write[i] =
				_reduce_type_argument(p_type.container_element_types[i], p_depth + 1);
	}
	return recorded;
}

} // namespace

BSConformanceRegistry::RecordedTypeArgument BSConformanceRegistry::reduce_type_argument(
		const BSParser::DataType &p_type) {
	return _reduce_type_argument(p_type, 0);
}

BSConformanceRegistry::ScopedVisibility::ScopedVisibility(const Visibility *p_visibility) {
	previous = active_visibility;
	active_visibility = p_visibility;
}

BSConformanceRegistry::ScopedVisibility::~ScopedVisibility() {
	active_visibility = previous;
}

BSConformanceRegistry::ScopedInFlightReplacement::ScopedInFlightReplacement(const String &p_source_file) {
	previous = has_in_flight_source_file ? String(in_flight_source_file) : String();
	const CharString utf8 = p_source_file.utf8();
	if (utf8.length() + 1 > (int)sizeof(in_flight_source_file)) {
		in_flight_source_file[0] = '\0';
		has_in_flight_source_file = false;
		return;
	}
	memcpy(in_flight_source_file, utf8.get_data(), utf8.length() + 1);
	has_in_flight_source_file = !p_source_file.is_empty();
}

BSConformanceRegistry::ScopedInFlightReplacement::~ScopedInFlightReplacement() {
	if (previous.is_empty()) {
		in_flight_source_file[0] = '\0';
		has_in_flight_source_file = false;
		return;
	}
	const CharString utf8 = previous.utf8();
	if (utf8.length() + 1 > (int)sizeof(in_flight_source_file)) {
		in_flight_source_file[0] = '\0';
		has_in_flight_source_file = false;
		return;
	}
	memcpy(in_flight_source_file, utf8.get_data(), utf8.length() + 1);
	has_in_flight_source_file = true;
}

bool BSConformanceRegistry::_is_visible(const String &p_source_file) {
	if (has_in_flight_source_file && p_source_file == String(in_flight_source_file)) {
		return false;
	}
	return active_visibility == nullptr || active_visibility->can_see(p_source_file);
}

BSConformanceRegistry *BSConformanceRegistry::get_singleton() {
	if (singleton == nullptr) {
		singleton = memnew(BSConformanceRegistry);
	}
	return singleton;
}

void BSConformanceRegistry::_rebuild_index() {
	index.clear();
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.trait_name == StringName()) {
				continue;
			}
			for (const String &target_key : conformance.target_keys) {
				if (target_key.is_empty()) {
					continue;
				}
				index[target_key][conformance.trait_name] = conformance.source_file;
			}
		}
	}
}

bool BSConformanceRegistry::_file_loads(const String &p_loader, const String &p_loaded) const {
	if (p_loader.is_empty() || p_loaded.is_empty()) {
		return false;
	}
	const HashSet<String> *loaded = loaded_files_by_file.getptr(p_loader);
	return loaded != nullptr && loaded->has(p_loaded);
}

bool BSConformanceRegistry::_candidate_conflicts_with_trait_binding(const Conformance &p_candidate,
		const String &p_source_file, RegistrationConflict &r_conflict) const {
	if (p_candidate.trait_name == StringName() || p_candidate.trait_type_arguments.is_empty()) {
		return false;
	}

	for (const KeyValue<String, Vector<ClassTraitBinding>> &file_entry : trait_bindings_by_file) {
		if (file_entry.key == p_source_file) {
			continue;
		}
		const bool joined_by_a_load_edge =
				_is_visible(file_entry.key) || _file_loads(file_entry.key, p_source_file);
		if (!joined_by_a_load_edge) {
			continue;
		}
		for (const ClassTraitBinding &binding : file_entry.value) {
			if (binding.trait_name != p_candidate.trait_name || binding.trait_type_arguments.is_empty() ||
					!_conformance_answers_for_binding(p_candidate, binding)) {
				continue;
			}
			if (!BSTypeCompatibility::recorded_arguments_conflict(binding.trait_type_arguments,
						p_candidate.trait_type_arguments)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::CHAIN_COHERENCE;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.conflicting_target_label =
					binding.target_label.is_empty() ? binding.target_fqcn : binding.target_label;
			r_conflict.conflicting_source_file = binding.source_file;
			return true;
		}
	}
	return false;
}

bool BSConformanceRegistry::_binding_conflicts_with_conformance(const ClassTraitBinding &p_binding,
		const String &p_source_file, const Vector<const Conformance *> &p_view,
		BindingConflict &r_conflict) const {
	if (p_binding.trait_name == StringName() || p_binding.trait_type_arguments.is_empty()) {
		return false;
	}
	for (const Conformance *existing : p_view) {
		if (existing->trait_name != p_binding.trait_name || existing->source_file == p_source_file ||
				existing->trait_type_arguments.is_empty()) {
			continue;
		}
		const bool joined_by_a_load_edge =
				_is_visible(existing->source_file) || _file_loads(existing->source_file, p_source_file);
		if (!joined_by_a_load_edge || !_conformance_answers_for_binding(*existing, p_binding)) {
			continue;
		}
		if (!BSTypeCompatibility::recorded_arguments_conflict(existing->trait_type_arguments,
					p_binding.trait_type_arguments)) {
			continue;
		}
		r_conflict.target_fqcn = p_binding.target_fqcn;
		r_conflict.target_label = p_binding.target_label.is_empty() ? p_binding.target_fqcn : p_binding.target_label;
		r_conflict.trait_name = p_binding.trait_name;
		r_conflict.trait_label = p_binding.trait_label.is_empty() ? String(p_binding.trait_name) : p_binding.trait_label;
		r_conflict.conflicting_target_label =
				existing->target_label.is_empty() ? existing->target_fqcn : existing->target_label;
		r_conflict.conflicting_source_file = existing->source_file;
		return true;
	}
	return false;
}

bool BSConformanceRegistry::_declaration_witnesses_collide(const Conformance &p_candidate,
		const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const {
	if (p_candidate.target_fqcn.is_empty() || p_candidate.witnesses.is_empty()) {
		return false;
	}
	// A witness collision is a property of the program, not of what one file loads: two files
	// supplying the same method name for one target contradict each other whether or not either
	// can see the other.
	for (const KeyValue<StringName, bool> &witness : p_candidate.witnesses) {
		if (witness.key == StringName()) {
			continue;
		}
		for (const Conformance *existing : p_view) {
			if (!existing->target_keys.has(p_candidate.target_fqcn) || !existing->witnesses.has(witness.key)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::WITNESS_COLLISION;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.method_name = witness.key;
			r_conflict.conflicting_target_label =
					existing->target_label.is_empty() ? existing->target_fqcn : existing->target_label;
			r_conflict.conflicting_source_file = existing->source_file;
			return true;
		}
	}
	return false;
}

bool BSConformanceRegistry::_candidate_conflicts(const Conformance &p_candidate, const String &p_source_file,
		const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const {
	if (p_candidate.trait_name == StringName()) {
		return false;
	}
	const auto reaches_candidate = [&](const Conformance &p_existing) {
		return p_existing.source_file == p_source_file || _is_visible(p_existing.source_file);
	};
	const bool candidate_is_native = _is_native_target(p_candidate);
	const StringName candidate_native_class = candidate_is_native ? StringName(p_candidate.target_fqcn) : StringName();

	if (!p_candidate.trait_type_arguments.is_empty()) {
		for (const Conformance *existing : p_view) {
			if (existing->trait_name != p_candidate.trait_name) {
				continue;
			}
			const bool existing_is_native = _is_native_target(*existing);
			bool answers_for_same_receivers = false;
			if (candidate_is_native && existing_is_native) {
				answers_for_same_receivers =
						_native_classes_are_on_one_chain(candidate_native_class, StringName(existing->target_fqcn));
			} else if (candidate_is_native) {
				answers_for_same_receivers = reaches_candidate(*existing) &&
						_native_ancestry_answers_for(existing->target_native_base, candidate_native_class);
			} else if (existing_is_native) {
				answers_for_same_receivers = reaches_candidate(*existing) &&
						_native_ancestry_answers_for(p_candidate.target_native_base, StringName(existing->target_fqcn));
			} else {
				answers_for_same_receivers =
						reaches_candidate(*existing) && _script_classes_are_on_one_chain(p_candidate, *existing);
			}
			if (!answers_for_same_receivers) {
				continue;
			}
			if (!BSTypeCompatibility::recorded_arguments_conflict(existing->trait_type_arguments,
						p_candidate.trait_type_arguments)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::CHAIN_COHERENCE;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.conflicting_target_label =
					existing->target_label.is_empty() ? existing->target_fqcn : existing->target_label;
			r_conflict.conflicting_source_file = existing->source_file;
			return true;
		}

		if (_candidate_conflicts_with_trait_binding(p_candidate, p_source_file, r_conflict)) {
			return true;
		}
	}

	if (!p_candidate.target_fqcn.is_empty()) {
		for (const Conformance *existing : p_view) {
			if (existing->trait_name != p_candidate.trait_name ||
					!existing->target_keys.has(p_candidate.target_fqcn)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::DUPLICATE_MEMBERSHIP;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.conflicting_target_label =
					existing->target_label.is_empty() ? existing->target_fqcn : existing->target_label;
			r_conflict.conflicting_source_file = existing->source_file;
			return true;
		}
	}

	return false;
}

void BSConformanceRegistry::register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_conformances.is_empty()) {
		conformances_by_file.erase(p_source_file);
	} else {
		Vector<Conformance> stored = p_conformances;
		for (int i = 0; i < stored.size(); i++) {
			stored.write[i].target_keys = _identifying_target_keys(
					stored[i].target_keys, stored[i].target_script_path, stored[i].target_is_root_class);
		}
		conformances_by_file[p_source_file] = stored;
	}
	_rebuild_index();
}

BSConformanceRegistry::RegistrationResult BSConformanceRegistry::try_replace_file_conformances(
		const String &p_source_file, const Vector<Conformance> &p_candidates,
		const Vector<ClassTraitBinding> &p_trait_bindings, const HashSet<String> &p_loaded_files) {
	RegistrationResult result;
	std::lock_guard<std::mutex> lock(mutex);

	Vector<Conformance> normalized = p_candidates;
	for (int i = 0; i < normalized.size(); i++) {
		normalized.write[i].target_keys = _identifying_target_keys(
				normalized[i].target_keys, normalized[i].target_script_path, normalized[i].target_is_root_class);
	}

	Vector<const Conformance *> view;
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (file_entry.key == p_source_file) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			view.push_back(&conformance);
		}
	}
	const int foreign_entry_count = view.size();

	Vector<Vector<int>> declarations;
	HashMap<int, int> declaration_by_conformance_index;
	for (int entry_index = 0; entry_index < normalized.size(); entry_index++) {
		const int conformance_index = normalized[entry_index].conformance_index;
		const int *declaration = declaration_by_conformance_index.getptr(conformance_index);
		if (declaration == nullptr) {
			declaration_by_conformance_index.insert(conformance_index, declarations.size());
			declarations.push_back(Vector<int>());
			declaration = declaration_by_conformance_index.getptr(conformance_index);
		}
		declarations.write[*declaration].push_back(entry_index);
	}

	Vector<Conformance> accepted;
	for (int d = 0; d < declarations.size(); d++) {
		const Vector<int> &declaration = declarations[d];
		RegistrationConflict conflict;
		bool conflicts = false;
		// Membership and chain coherence are per identity; the witness map is shared by every entry
		// the declaration emitted, so it is checked once, and last, so a contradiction is reported
		// as the membership or chain contradiction it is rather than as the witness collision it
		// also implies.
		for (int position = 0; position < declaration.size() && !conflicts; position++) {
			conflicts = _candidate_conflicts(normalized[declaration[position]], p_source_file, view, conflict);
		}
		if (!conflicts) {
			conflicts = _declaration_witnesses_collide(normalized[declaration[0]], view, conflict);
		}
		if (conflicts) {
			result.conflicts.push_back(conflict);
		} else {
			for (int position = 0; position < declaration.size(); position++) {
				accepted.push_back(normalized[declaration[position]]);
			}
			view.resize(foreign_entry_count);
			for (const Conformance &entry : accepted) {
				view.push_back(&entry);
			}
		}
	}

	// The load edges this file resolved are published with what it declares: they are what lets the file
	// at the other end of an edge, judging its own declarations later, tell that these were licensed to
	// be compared against it. Both the conformance side and the binding side read them, so a file that
	// publishes either records its edges -- and a file that publishes neither has nothing to license and
	// stays out of the store.
	if (p_loaded_files.is_empty() || (accepted.is_empty() && p_trait_bindings.is_empty())) {
		loaded_files_by_file.erase(p_source_file);
	} else {
		loaded_files_by_file[p_source_file] = p_loaded_files;
	}

	for (const ClassTraitBinding &binding : p_trait_bindings) {
		BindingConflict binding_conflict;
		if (_binding_conflicts_with_conformance(binding, p_source_file, view, binding_conflict)) {
			result.binding_conflicts.push_back(binding_conflict);
		}
	}
	if (accepted.is_empty()) {
		conformances_by_file.erase(p_source_file);
	} else {
		conformances_by_file[p_source_file] = accepted;
	}
	if (p_trait_bindings.is_empty()) {
		trait_bindings_by_file.erase(p_source_file);
	} else {
		trait_bindings_by_file[p_source_file] = p_trait_bindings;
	}
	_rebuild_index();

	result.registered_count = accepted.size();
	return result;
}

void BSConformanceRegistry::clear_file(const String &p_source_file) {
	std::lock_guard<std::mutex> lock(mutex);
	trait_bindings_by_file.erase(p_source_file);
	loaded_files_by_file.erase(p_source_file);
	if (conformances_by_file.erase(p_source_file)) {
		_rebuild_index();
	}
}

void BSConformanceRegistry::clear() {
	std::lock_guard<std::mutex> lock(mutex);
	conformances_by_file.clear();
	trait_bindings_by_file.clear();
	loaded_files_by_file.clear();
	index.clear();
}

void BSConformanceRegistry::clear_declarations() {
	std::lock_guard<std::mutex> lock(mutex);
	conformances_by_file.clear();
	trait_bindings_by_file.clear();
	loaded_files_by_file.clear();
	index.clear();
}

bool BSConformanceRegistry::has_conformance(const String &p_target_key, const StringName &p_trait_name) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	return _has_visible_conformance(p_target_key, p_trait_name);
}

bool BSConformanceRegistry::_has_visible_conformance(const String &p_target_key, const StringName &p_trait_name) const {
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits == nullptr) {
		return false;
	}
	const String *source_file = traits->getptr(p_trait_name);
	return source_file != nullptr && _is_visible(*source_file);
}

bool BSConformanceRegistry::_recorded_trait_arguments_for_key(const String &p_target_key,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (!_has_visible_conformance(p_target_key, p_trait_name)) {
		return false;
	}
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	const Vector<Conformance> *entries = conformances_by_file.getptr(*traits->getptr(p_trait_name));
	if (entries == nullptr) {
		return false;
	}
	for (const Conformance &conformance : *entries) {
		if (conformance.trait_name != p_trait_name || !conformance.target_keys.has(p_target_key)) {
			continue;
		}
		if (conformance.trait_type_arguments.is_empty()) {
			return false;
		}
		r_arguments = conformance.trait_type_arguments;
		return true;
	}
	return false;
}

bool BSConformanceRegistry::get_recorded_trait_arguments(const String &p_target_key,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	return _recorded_trait_arguments_for_key(p_target_key, p_trait_name, r_arguments);
}

bool BSConformanceRegistry::get_native_recorded_trait_arguments(const StringName &p_native_class,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (p_native_class == StringName() || p_trait_name == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	// The nearest conforming ancestor wins, matching how membership itself is answered. A hidden
	// conformance does not shadow a visible one further up, for the same reason.
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		const String key = String(cursor);
		if (_has_visible_conformance(key, p_trait_name)) {
			return _recorded_trait_arguments_for_key(key, p_trait_name, r_arguments);
		}
	}
	return false;
}

bool BSConformanceRegistry::get_builtin_recorded_trait_arguments(Variant::Type p_type,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_trait_name == StringName()) {
		return false;
	}
	// Lock here rather than calling get_recorded_trait_arguments: std::mutex is not recursive.
	std::lock_guard<std::mutex> lock(mutex);
	return _recorded_trait_arguments_for_key(Variant::get_type_name(p_type), p_trait_name, r_arguments);
}

bool BSConformanceRegistry::native_class_conforms(const StringName &p_native_class,
		const StringName &p_trait_name) const {
	if (p_native_class == StringName() || p_trait_name == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		if (_has_visible_conformance(String(cursor), p_trait_name)) {
			return true;
		}
	}
	return false;
}

bool BSConformanceRegistry::builtin_type_conforms(Variant::Type p_type, const StringName &p_trait_name) const {
	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_trait_name == StringName()) {
		return false;
	}
	return has_conformance(Variant::get_type_name(p_type), p_trait_name);
}

String BSConformanceRegistry::get_conformance_source(const String &p_target_key, const StringName &p_trait_name) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return String();
	}
	std::lock_guard<std::mutex> lock(mutex);
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits == nullptr) {
		return String();
	}
	const String *source = traits->getptr(p_trait_name);
	return source != nullptr ? *source : String();
}

String BSConformanceRegistry::get_witness_source(const String &p_target_key, const StringName &p_method,
		StringName &r_trait_name) const {
	r_trait_name = StringName();
	if (p_target_key.is_empty() || p_method == StringName()) {
		return String();
	}
	std::lock_guard<std::mutex> lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		for (const Conformance &conformance : file_entry.value) {
			if (!conformance.target_keys.has(p_target_key)) {
				continue;
			}
			if (conformance.witnesses.has(p_method)) {
				r_trait_name = conformance.trait_name;
				return conformance.source_file;
			}
		}
	}
	return String();
}

Vector<BSConformanceRegistry::Conformance> BSConformanceRegistry::get_file_conformances(const String &p_source_file) const {
	std::lock_guard<std::mutex> lock(mutex);
	const Vector<Conformance> *entries = conformances_by_file.getptr(p_source_file);
	return entries != nullptr ? *entries : Vector<Conformance>();
}

Vector<BSConformanceRegistry::ClassTraitBinding> BSConformanceRegistry::get_file_trait_bindings(
		const String &p_source_file) const {
	std::lock_guard<std::mutex> lock(mutex);
	const Vector<ClassTraitBinding> *entries = trait_bindings_by_file.getptr(p_source_file);
	return entries != nullptr ? *entries : Vector<ClassTraitBinding>();
}

bool BSConformanceRegistry::find_witness_location(const String &p_target_fqcn, const StringName &p_method,
		String &r_source_file, int &r_conformance_index) const {
	r_source_file = String();
	r_conformance_index = -1;
	if (p_target_fqcn.is_empty() || p_method == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (!_is_visible(file_entry.key)) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.conformance_index < 0 || conformance.target_fqcn != p_target_fqcn ||
					!conformance.witnesses.has(p_method)) {
				continue;
			}
			r_source_file = conformance.source_file;
			r_conformance_index = conformance.conformance_index;
			return true;
		}
	}
	return false;
}

bool BSConformanceRegistry::find_hidden_witness_declaration(const String &p_target_fqcn, const StringName &p_method,
		String &r_source_file, StringName &r_trait_name) const {
	r_source_file = String();
	r_trait_name = StringName();
	if (p_target_fqcn.is_empty() || p_method == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (_is_visible(file_entry.key)) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.target_fqcn != p_target_fqcn || !conformance.witnesses.has(p_method)) {
				continue;
			}
			r_source_file = conformance.source_file;
			r_trait_name = conformance.trait_name;
			return true;
		}
	}
	return false;
}

BSConformanceRegistry::BSConformanceRegistry() {
	if (singleton == nullptr) {
		singleton = this;
	}
}

BSConformanceRegistry::~BSConformanceRegistry() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

} // namespace barista_script
