/**************************************************************************/
/*  bs_conformance_registry.h                                             */
/*                                                                        */
/*  Hard fork of Foundry fs_conformance_registry.h @ c9d5e35. Starter:   */
/*  Visibility / ScopedVisibility / ScopedInFlightReplacement +          */
/*  declaration store with atomic try_replace_file_conformances +        */
/*  witness method-name keys / find_witness_location /                   */
/*  find_hidden_witness_declaration + RecordedTypeArgument /             */
/*  ClassTraitBinding chain-coherence against uses bindings +            */
/*  p_loaded_files load-graph licensing + declaration-side               */
/*  get_recorded_trait_arguments / get_native_recorded_trait_arguments / */
/*  get_builtin_recorded_trait_arguments. Runtime Function* witnesses    */
/*  and assignability call-site wiring remain residual under #60.        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_parser.h"
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
	 * `FunctionNode *` values; Barista stores `true` placeholders so lookups never
	 * dereference borrowed pointers across reloads. `find_conformance_witness`
	 * re-finds live nodes from the declaring parse tree via conformance_index.
	 */
	using WitnessMap = HashMap<StringName, bool>;

	/**
	 * Flattened identity for one type-argument position. Anything this form cannot
	 * represent with certainty is `UNKNOWN` — an absence of evidence, never a
	 * wildcard. Foundry `RecordedTypeArgument` @ c9d5e35; D1 omits NumericType.
	 */
	struct RecordedTypeArgument {
		enum Kind : uint8_t {
			UNKNOWN,
			BUILTIN,
			NATIVE_CLASS,
			SCRIPT_CLASS,
		};
		Kind kind = UNKNOWN;
		bool is_nullable = false;
		Variant::Type builtin_type = Variant::NIL;
		StringName native_class;
		String script_fqcn;
		String script_global_name;
		Vector<RecordedTypeArgument> type_arguments;
		Vector<RecordedTypeArgument> container_element_types;
	};

	/** Single reduction both sides of a recorded-argument comparison go through. */
	static RecordedTypeArgument reduce_type_argument(const BSParser::DataType &p_type);

	/**
	 * One declaration-side conformance entry. Runtime Function* witnesses remain
	 * residual; witness *names* + index + recorded trait arguments are enough for
	 * visibility-filtered membership, member-miss re-resolve, and uses-binding
	 * chain coherence.
	 */
	struct Conformance {
		Vector<String> target_keys;
		String target_fqcn;
		String target_script_path;
		bool target_is_root_class = true;
		StringName trait_name;
		/**
		 * Arguments this conformance supplied for `trait_name`. Empty when the trait
		 * is not generic or the declaration supplied none — absence of evidence.
		 */
		Vector<RecordedTypeArgument> trait_type_arguments;
		StringName target_native_base;
		Vector<String> target_script_ancestor_fqcns;
		String target_label;
		String source_file;
		int conformance_index = -1;
		WitnessMap witnesses;
	};

	/**
	 * One class's own `uses` clause as another file's coherence check needs to see
	 * it. Registers no membership; only bindings that actually supplied arguments
	 * are recorded. Foundry `ClassTraitBinding` @ c9d5e35.
	 */
	struct ClassTraitBinding {
		String target_fqcn;
		String target_label;
		StringName target_native_base;
		Vector<String> target_script_ancestor_fqcns;
		StringName trait_name;
		String trait_label;
		Vector<RecordedTypeArgument> trait_type_arguments;
		String source_file;
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
			// Reserved for follow-up witness ports under #60.
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
	 * Contradiction found for a submitted ClassTraitBinding. Reported, never
	 * arbitrated: every binding is stored regardless.
	 */
	struct BindingConflict {
		String target_fqcn;
		String target_label;
		StringName trait_name;
		String trait_label;
		String conflicting_target_label;
		String conflicting_source_file;
	};

	/**
	 * Outcome of one atomic validate-and-replace. `registered_count` is the
	 * candidate set minus every entry belonging to a rejected declaration.
	 */
	struct RegistrationResult {
		Vector<RegistrationConflict> conflicts;
		Vector<BindingConflict> binding_conflicts;
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
	HashMap<String, Vector<ClassTraitBinding>> trait_bindings_by_file;
	// Cross-file load licensing graph. A file that publishes conformances or class-`uses`
	// bindings records the files it loads so a later analysis on the other end of an edge
	// can still join them when its own directional Visibility cannot reach back.
	HashMap<String, HashSet<String>> loaded_files_by_file;

	void _rebuild_index();

	/** Callers must hold `mutex`. */
	bool _has_visible_conformance(const String &p_target_key, const StringName &p_trait_name) const;
	bool _recorded_trait_arguments_for_key(const String &p_target_key, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	/** Callers must hold `mutex`. */
	bool _candidate_conflicts(const Conformance &p_candidate, const String &p_source_file,
			const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const;

	bool _candidate_conflicts_with_trait_binding(const Conformance &p_candidate, const String &p_source_file,
			RegistrationConflict &r_conflict) const;

	bool _file_loads(const String &p_loader, const String &p_loaded) const;

	bool _binding_conflicts_with_conformance(const ClassTraitBinding &p_binding, const String &p_source_file,
			const Vector<const Conformance *> &p_view, BindingConflict &r_conflict) const;

public:
	static BSConformanceRegistry *get_singleton();

	/** Replaces every conformance previously registered by `p_source_file`. */
	void register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances);

	/**
	 * Validates `p_candidates` against the rest of the registry and replaces
	 * `p_source_file`'s entries with the ones that survive, under one lock.
	 *
	 * Previous entries stay visible to other readers until this commits. An empty
	 * candidate list clears the file's conformances. `p_trait_bindings` replaces
	 * class-`uses` bindings in the same indivisible step. `p_loaded_files` is the
	 * set of files `p_source_file` loads; recorded whenever the file publishes
	 * accepted conformances or bindings (erased when empty / nothing to license).
	 */
	RegistrationResult try_replace_file_conformances(const String &p_source_file,
			const Vector<Conformance> &p_candidates,
			const Vector<ClassTraitBinding> &p_trait_bindings = Vector<ClassTraitBinding>(),
			const HashSet<String> &p_loaded_files = HashSet<String>());

	void clear_file(const String &p_source_file);
	void clear();
	void clear_declarations();

	/**
	 * True when some *visible* target alias declares an external conformance to
	 * `p_trait_name`. Empty store → false (fail closed).
	 */
	bool has_conformance(const String &p_target_key, const StringName &p_trait_name) const;

	/**
	 * The type arguments a *visible declaration-side* conformance of `p_target_key` to
	 * `p_trait_name` recorded. False when no visible conformance exists or it recorded none —
	 * both an absence of evidence, never a wildcard. Deliberately not the runtime store.
	 * Foundry `get_recorded_trait_arguments` @ c9d5e35.
	 */
	bool get_recorded_trait_arguments(const String &p_target_key, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	/**
	 * The same, for an engine class. Walks `ClassDB::get_parent_class`; the nearest conforming
	 * ancestor wins, matching Foundry `native_class_conforms` / membership reach.
	 */
	bool get_native_recorded_trait_arguments(const StringName &p_native_class,
			const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const;

	/**
	 * The same, for a builtin value type. Builtins have no inheritance chain, so this is an
	 * exact-key lookup keyed by `Variant::get_type_name`.
	 */
	bool get_builtin_recorded_trait_arguments(Variant::Type p_type, const StringName &p_trait_name,
			Vector<RecordedTypeArgument> &r_arguments) const;

	String get_conformance_source(const String &p_target_key, const StringName &p_trait_name) const;
	Vector<Conformance> get_file_conformances(const String &p_source_file) const;
	Vector<ClassTraitBinding> get_file_trait_bindings(const String &p_source_file) const;

	/**
	 * Locates the visible conformance that supplies a witness for `p_method` on
	 * the target whose fully-qualified class name is `p_target_fqcn`. Matching is
	 * on the exact FQCN (not looser aliases). Returns declaring file +
	 * conformance_index only — never a borrowed FunctionNode.
	 */
	bool find_witness_location(const String &p_target_fqcn, const StringName &p_method,
			String &r_source_file, int &r_conformance_index) const;

	/**
	 * The declaring file and trait of a witness for `(p_target_fqcn, p_method)` that
	 * the installed Visibility *hides*. Inverse of `find_witness_location`: reports
	 * conformances a caller must not type-check against, so an unresolved call can
	 * be rejected with the reason. Matches on the exact FQCN.
	 */
	bool find_hidden_witness_declaration(const String &p_target_fqcn, const StringName &p_method,
			String &r_source_file, StringName &r_trait_name) const;

#ifdef DEBUG_ENABLED
	/** Test surface for ScopedVisibility / in-flight hiding (wraps `_is_visible`). */
	static bool debug_is_visible(const String &p_source_file) { return _is_visible(p_source_file); }
#endif

	BSConformanceRegistry();
	~BSConformanceRegistry();
};

} // namespace barista_script
