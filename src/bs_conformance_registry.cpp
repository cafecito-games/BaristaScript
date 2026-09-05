/**************************************************************************/
/*  bs_conformance_registry.cpp                                           */
/*                                                                        */
/*  Hard fork of Foundry fs_conformance_registry.cpp @ c9d5e35. Visibility*/
/*  stack + declaration store with atomic try_replace_file_conformances  */
/*  + find_witness_location / find_hidden_witness_declaration            */
/*  (method-name keys). ClassTraitBinding / RecordedTypeArgument /       */
/*  runtime Function* witnesses remain residual.                         */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "bs_conformance_registry.h"

#include <cstring>

namespace barista_script {

BSConformanceRegistry *BSConformanceRegistry::singleton = nullptr;
thread_local const BSConformanceRegistry::Visibility *BSConformanceRegistry::active_visibility = nullptr;
thread_local bool BSConformanceRegistry::has_in_flight_source_file = false;
thread_local char BSConformanceRegistry::in_flight_source_file[1024] = {};

namespace {

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

} // namespace

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

bool BSConformanceRegistry::_candidate_conflicts(const Conformance &p_candidate,
		const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const {
	if (p_candidate.trait_name == StringName() || p_candidate.target_fqcn.is_empty()) {
		return false;
	}
	// Duplicate membership: match the candidate's exact FQCN against the other side's alias set —
	// the same identity relation the flattened lookup index answers with.
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
		const String &p_source_file, const Vector<Conformance> &p_candidates) {
	RegistrationResult result;
	std::lock_guard<std::mutex> lock(mutex);

	Vector<Conformance> normalized = p_candidates;
	for (int i = 0; i < normalized.size(); i++) {
		normalized.write[i].target_keys = _identifying_target_keys(
				normalized[i].target_keys, normalized[i].target_script_path, normalized[i].target_is_root_class);
	}

	// View excludes this file's previous entries so unchanged reanalysis cannot conflict with itself;
	// those entries stay in the store until the replacement below commits.
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

	// Group candidates by ConformanceNode index: a conflict on any identity a declaration emits
	// drops every entry it emitted. Groups keep first-seen order.
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
		for (int position = 0; position < declaration.size() && !conflicts; position++) {
			conflicts = _candidate_conflicts(normalized[declaration[position]], view, conflict);
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

	if (accepted.is_empty()) {
		conformances_by_file.erase(p_source_file);
	} else {
		conformances_by_file[p_source_file] = accepted;
	}
	_rebuild_index();

	result.registered_count = accepted.size();
	return result;
}

void BSConformanceRegistry::clear_file(const String &p_source_file) {
	std::lock_guard<std::mutex> lock(mutex);
	if (conformances_by_file.erase(p_source_file)) {
		_rebuild_index();
	}
}

void BSConformanceRegistry::clear() {
	std::lock_guard<std::mutex> lock(mutex);
	conformances_by_file.clear();
	index.clear();
}

void BSConformanceRegistry::clear_declarations() {
	std::lock_guard<std::mutex> lock(mutex);
	conformances_by_file.clear();
	index.clear();
}

bool BSConformanceRegistry::has_conformance(const String &p_target_key, const StringName &p_trait_name) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	std::lock_guard<std::mutex> lock(mutex);
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits == nullptr) {
		return false;
	}
	const String *source_file = traits->getptr(p_trait_name);
	return source_file != nullptr && _is_visible(*source_file);
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

Vector<BSConformanceRegistry::Conformance> BSConformanceRegistry::get_file_conformances(const String &p_source_file) const {
	std::lock_guard<std::mutex> lock(mutex);
	const Vector<Conformance> *entries = conformances_by_file.getptr(p_source_file);
	return entries != nullptr ? *entries : Vector<Conformance>();
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
