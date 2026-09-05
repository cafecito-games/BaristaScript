/**************************************************************************/
/*  bs_conformance_registry.cpp                                           */
/*                                                                        */
/*  Hard fork of Foundry fs_conformance_registry.cpp @ c9d5e35. Visibility*/
/*  stack + empty/seedable declaration store; full try_replace / runtime */
/*  witness / RecordedTypeArgument coherence remains residual under #60. */
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

void BSConformanceRegistry::register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances) {
	std::lock_guard<std::mutex> lock(mutex);
	if (p_conformances.is_empty()) {
		conformances_by_file.erase(p_source_file);
	} else {
		conformances_by_file[p_source_file] = p_conformances;
	}
	_rebuild_index();
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
