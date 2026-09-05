/**************************************************************************/
/*  bs_conformance_registry.h                                             */
/*                                                                        */
/*  Hard fork of Foundry fs_conformance_registry.h @ c9d5e35. Starter:   */
/*  Visibility / ScopedVisibility / ScopedInFlightReplacement +          */
/*  declaration store with atomic try_replace_file_conformances +        */
/*  witness method-name keys / find_witness_location. ClassTraitBinding / */
/*  RecordedTypeArgument coherence / runtime Function* witnesses remain  */
/*  residual under #60.                                                   */
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
 * Visibility stack + declaration store. Analyzer `resolve_conformances`
 * publishes validated entries through `try_replace_file_conformances` under
 * `ScopedInFlightReplacement` so reanalysis never leaves a visibility gap and
 * never reads its own previous declarations mid-pass.
 */
class BSConformanceRegistry {
public:
	/**
	 * Method-name keys for witnesses on one (target, trait) entry. Foundry stores
	 * `FunctionNode *` values; Barista stores names only so lookups never
	 * dereference borrowed pointers across reloads. `find_conformance_witness`
	 * re-finds live nodes from the declaring parse tree via conformance_index.
	 */
	using WitnessMap = HashSet<StringName>;

	/**
	 * One declaration-side conformance entry. RecordedTypeArgument coherence and
	 * runtime Function* witnesses remain residual; witness *names* + index are
	 * enough for visibility-filtered membership and member-miss re-resolve.
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
		WitnessMap witnesses;
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

	/**
	 * One candidate declaration the registry refused. Value-only so diagnostics
	 * can be produced after the registry lock is released.
	 */
	struct RegistrationConflict {
		enum Kind : uint8_t {
			DUPLICATE_MEMBERSHIP,
			// Reserved for follow-up witness / chain ports under #60.
			WITNESS_COLLISION,
			CHAIN_COHERENCE,
		};
		Kind kind = DUPLICATE_MEMBERSHIP;
		int conformance_index = -1;
		String target_label;
		StringName trait_name;
		StringName method_name;
		String conflicting_target_label;
		String conflicting_source_file;
	};

	/**
	 * Outcome of one atomic validate-and-replace. `registered_count` is the
	 * candidate set minus every entry belonging to a rejected declaration.
	 */
	struct RegistrationResult {
		Vector<RegistrationConflict> conflicts;
		int registered_count = 0;
	};

private:
	static BSConformanceRegistry *singleton;
	static thread_local const Visibility *active_visibility;
	// Prefer a POD TLS slot over `thread_local String`: godot-cpp String goes through the
	// GDExtension allocator and is not safe as a thread_local object (teardown / first-touch).
	static thread_local bool has_in_flight_source_file;
	static thread_local char in_flight_source_file[1024];

	static bool _is_visible(const String &p_source_file);

	mutable std::mutex mutex;

	HashMap<String, Vector<Conformance>> conformances_by_file;
	HashMap<String, HashMap<StringName, String>> index;

	void _rebuild_index();

	/** Callers must hold `mutex`. */
	bool _candidate_conflicts(const Conformance &p_candidate, const Vector<const Conformance *> &p_view,
			RegistrationConflict &r_conflict) const;

public:
	static BSConformanceRegistry *get_singleton();

	/** Replaces every conformance previously registered by `p_source_file`. */
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	/**
	 * Validates `p_candidates` against the rest of the registry and replaces
	 * `p_source_file`'s entries with the ones that survive, under one lock.
	 *
	 * Previous entries stay visible to other readers until this commits. An empty
	 * candidate list clears the file. ClassTraitBinding / chain-coherence /
	 * witness-collision arbitration remain residual under #60; this slice
	 * rejects duplicate (target FQCN, trait) membership only.
	 */
	RegistrationResult try_replace_file_conformances(const String &p_source_file,
			const Vector<Conformance> &p_candidates);

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

	/**
	 * Locates the visible conformance that supplies a witness for `p_method` on
	 * the target whose fully-qualified class name is `p_target_fqcn`. Matching is
	 * on the exact FQCN (not looser aliases). Returns declaring file +
	 * conformance_index only — never a borrowed FunctionNode.
	 */
	bool find_witness_location(const String &p_target_fqcn, const StringName &p_method,
			String &r_source_file, int &r_conformance_index) const;

#ifdef DEBUG_ENABLED
	/** Test surface for ScopedVisibility / in-flight hiding (wraps `_is_visible`). */
	static bool debug_is_visible(const String &p_source_file) { return _is_visible(p_source_file); }
#endif

	BSConformanceRegistry();
	~BSConformanceRegistry();
};

} // namespace barista_script
