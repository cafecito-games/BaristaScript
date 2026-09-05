/**************************************************************************/
/*  bs_conformance_registry.h                                             */
/*                                                                        */
/*  Hard fork of Foundry fs_conformance_registry.h @ c9d5e35. Starter:   */
/*  Visibility / ScopedVisibility / ScopedInFlightReplacement + empty    */
/*  declaration store. Full registration / witness / runtime coherence   */
/*  remains residual under #60.                                           */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_platform.h"

#include <mutex>

namespace barista_script {

/**
 * Process-global registry of retroactive trait conformances.
 *
 * This slice ports Foundry's visibility stack and an empty declaration store so
 * analyzer filtering can install a thread-local `Visibility` and later queries
 * fail closed ("not found" / invisible) until registration lands under #60.
 */
class BSConformanceRegistry {
public:
	/**
	 * One declaration-side conformance entry. Witness maps, RecordedTypeArgument
	 * coherence, and runtime witnesses are deferred; the fields here are enough
	 * for visibility-filtered membership lookups against an empty or seeded store.
	 */
	struct Conformance {
		Vector<String> target_keys;
		String target_fqcn;
		String target_script_path;
		bool target_is_root_class = true;
		StringName trait_name;
		StringName target_native_base;
		Vector<String> target_script_ancestor_fqcns;
		String target_label;
		String source_file;
		int conformance_index = -1;
	};

	/** Limits which declaring files a caller is allowed to see. */
	class Visibility {
	public:
		virtual bool can_see(const String &p_source_file) const = 0;
		virtual ~Visibility() = default;
	};

	/**
	 * Installs a `Visibility` for the current thread until it goes out of scope.
	 * Nests: the previous visibility is restored on destruction.
	 */
	class ScopedVisibility {
		const Visibility *previous = nullptr;

	public:
		explicit ScopedVisibility(const Visibility *p_visibility);
		~ScopedVisibility();
	};

	/**
	 * Hides one declaring file from *this thread's* registry queries until scope
	 * exit (in-flight reanalysis must not read its own previous declarations).
	 */
	class ScopedInFlightReplacement {
		String previous;

	public:
		explicit ScopedInFlightReplacement(const String &p_source_file);
		~ScopedInFlightReplacement();
	};

private:
	static BSConformanceRegistry *singleton;
	static thread_local const Visibility *active_visibility;
	static thread_local String in_flight_source_file;

	static bool _is_visible(const String &p_source_file);

	mutable std::mutex mutex;

	HashMap<String, Vector<Conformance>> conformances_by_file;
	HashMap<String, HashMap<StringName, String>> index;

	void _rebuild_index();

public:
	static BSConformanceRegistry *get_singleton();

	/** Replaces every conformance previously registered by `p_source_file`. */
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	void clear_file(const String &p_source_file);
	void clear();
	void clear_declarations();

	/**
	 * True when some *visible* target alias declares an external conformance to
	 * `p_trait_name`. Empty store → false (fail closed).
	 */
	bool has_conformance(const String &p_target_key, const StringName &p_trait_name) const;

	String get_conformance_source(const String &p_target_key, const StringName &p_trait_name) const;
	Vector<Conformance> get_file_conformances(const String &p_source_file) const;

#ifdef DEBUG_ENABLED
	/** Test surface for ScopedVisibility / in-flight hiding (wraps `_is_visible`). */
	static bool debug_is_visible(const String &p_source_file) { return _is_visible(p_source_file); }
#endif

	BSConformanceRegistry();
	~BSConformanceRegistry();
};

} // namespace barista_script
