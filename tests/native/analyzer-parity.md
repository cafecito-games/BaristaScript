# Analyzer migration parity inventory

This inventory is reconciled with `project/tests/analyzer_test.gd` at merged main
`4b2439fb95a4ed6069c3771ea1663ae50eeb8ac1` (PR #199). The suite still invokes exactly
90 unique scenarios. A normal merge at `fbc37cad2a4aa2e20694863f8b79106810617379`
preserves the original migration commits `9c5e203`, `19085cd`, and `14343b9`.

The legacy suite remains enabled: 46 scenarios have explicit native equivalents and 44
still require migration. No analyzer, cache, index, public, corpus, or triage adapter
has been removed. Counts in the historical checkpoint sections describe those checkpoints;
current execution evidence is recorded separately below.

## Additive #139 native foundation

The seven functions added after the issue's `71d7883a` baseline are registered as seven filterable
cases in native suite `analyzer_flow`. They use `BSParser`, `BSAnalyzer`, `BSWarning`, typed AST
nodes, `StorageFixture`, and fail-closed helper guards. The one non-source-reachable exhaustion
control still calls the existing bounded debug probe for private analyzer internals; its carrier
compatibility assertions call `BSTypeCompatibility` directly.

| Legacy function | Commit | Native case | Preserved fixture inventory |
| --- | --- | --- | ---: |
| `_test_pinned_suite_exit_summary` | `ed68d67` | `pinned_suite_exit_summary` | 56 |
| `_test_pinned_match_finality_domains` | `b3e61bf` | `pinned_match_finality_domains` | 56 |
| `_test_internal_type_test_exhaustion` | `b3e61bf` | `internal_type_test_exhaustion` | 8 private-helper controls plus typed compatibility matrix |
| `_test_pinned_match_domain_and_narrowing_audit` | `b3e61bf` | `pinned_match_domain_and_narrowing_audit` | 70 |
| `_test_pinned_for_assert_consumers` | `f8d954c` | `pinned_for_assert_consumers` | 50 |
| `_test_pinned_suite_datatypes` | `f8d954c` | `pinned_suite_datatypes` | 18 sources plus 22 iterator carriers |
| `_test_native_iterator_annotation_nullability` | `26f1460` | `native_iterator_annotation_nullability` | 9 |

## Additive cache, settings and public-validity checkpoint

Five more legacy functions are registered under native suite `analyzer_cache`. The source contains
36 legacy `_expect` calls; the native cases preserve every condition as 47 direct typed assertions
and guards. Per-case supervisor counts include the `StorageFixture` setup and teardown assertions.
The sixth case runs all five scenarios twice in forward and reverse order inside the ambient-state
isolation verifier.

| Legacy function | Native case | Legacy `_expect` calls | Native run assertions | Preserved contract |
| --- | --- | ---: | ---: | --- |
| `_test_parser_lifecycle` | `parser_lifecycle` | 4 | 13 | PARSED/FULLY_SOLVED monotonic raise, latched hash, cached identity and presence. |
| `_test_missing_and_self` | `missing_and_self` | 7 | 11 | Missing admission and inverse-edge rejection, ghost-removal safety, self admission without a self-edge. |
| `_test_strict_settings` | `strict_settings` | 9 | 13 | Baseline/no-op observations, independent null/dynamic flips, parser invalidation, and override retention. |
| `_test_can_reference` | `can_reference` | 10 | 12 | Builtin, meta, union, tuple, native ancestry and failed class-path reference decisions through `BSParser::DataType`. |
| `_test_validate_and_is_valid_agree` | `validate_and_is_valid_agree` | 6 | 8 | Typed parse/analyze, `bs_source_analyzes`, and `BaristaScript::_is_valid()` agree for valid and semantic-error sources. |

The complete suite executes 6 cases and 705 assertions: 57 assertions across the five individually
filterable cases, then 648 assertions in `repeated_and_reversed_cases_restore_ambient_state`.

## Additive type-compatibility checkpoint

Six more legacy functions are mapped one-for-one into native suite
`analyzer_type_compatibility`. Their 58 legacy `_expect` contracts are preserved with direct
`BSParser`/`BSAnalyzer` source analysis, ordered parser-error checks, typed AST/`DataType`
inspection, warning-absence checks, and declaration-index isolation. A seventh case repeats all
six scenarios in forward and reverse order under the ambient-state isolation verifier.

| Legacy function | Native case | Preserved contracts |
| --- | --- | ---: |
| `_test_union_union_assignability` | `union_union_assignability` | 10 |
| `_test_union_store_carrier_select` | `union_store_carrier_select` | 8 |
| `_test_enum_self_payload_field_leg` | `enum_self_payload_field_leg` | 13 |
| `_test_self_contract_assign_return` | `self_contract_assign_return` | 11 |
| `_test_self_contract_gradual_union` | `self_contract_gradual_union` | 8 |
| `_test_ordinary_assignment_and_return_consumers` | `ordinary_assignment_and_return_consumers` | 8 |

`_test_complete_self_referential_enum_type` and `_test_local_tuple_and_literal_consumers` remain
legacy-only and are intentionally outside this checkpoint.

## Historical #140 native checkpoint coverage

At that historical checkpoint, issue #140 owned 98 already-registered native cases. They are preserved unchanged and are not
duplicated here. Their responsibility map is:

| Native suite | Cases | Responsibility | Legacy scenarios it supplements (not yet a deletion-equivalent mapping) |
| --- | ---: | --- | --- |
| `cross_file_analyzer` | 16 | imported-name precedence, ambiguity, stale providers, bootstrap and typed lookup | parser/dependency lifecycle, strict settings, declaration commit, namespace/import validation |
| `provider_analyzer` | 21 | provider ownership, member surfaces, retained generations/failures, witness, nullable heads and refresh replay | callable/member depth, foreign failure replay, trait/conformance and enum surface scenarios |
| `preload_analyzer` | 31 | preload identity, failure replay, recursive ownership, semantic containers | dependency cycles, explicit paths, provider failure replay, constant/container scenarios |
| `refresh_analyzer` | 13 | refresh publication, invalidation, move/remove, digest/generation and namespace repair | transitive invalidation, move/remove, declaration commit, digest mismatch, namespace change |
| `source_analyzer` | 17 | public source-input isolation, retained generations, read-only lookup and construction/call diagnostics | parser/dependency lifecycle, validation agreement and callable/member diagnostics |

The exact `TEST_CASE` registrations in those five issue-owned source files remain the authoritative
#140 case-name inventory. This checkpoint neither edits nor copies them.

## Current 90-function inventory

`native #139` means this checkpoint has an explicit equivalent case above. `legacy + #140` means
#140 has related typed coverage but the legacy function still contains distinct assertions. Rows marked with another native suite have an explicit same-name native case. The remaining
legacy rows retain distinct assertions and cannot be deleted based on related semantic coverage.

| # | Invoked function | Group | Status |
| ---: | --- | --- | --- |
| 1 | `_test_parser_lifecycle` | dependency reanalysis | native `analyzer_cache` |
| 2 | `_test_transitive_invalidation` | dependency reanalysis | native `analyzer_dependency` |
| 3 | `_test_missing_and_self` | dependency reanalysis | native `analyzer_cache` |
| 4 | `_test_move_remove` | dependency reanalysis | native `analyzer_dependency` |
| 5 | `_test_dependency_cycle` | dependency reanalysis | native `analyzer_dependency` |
| 6 | `_test_finalization_raises_dependencies` | dependency reanalysis | native `analyzer_dependency` |
| 7 | `_test_strict_settings` | diagnostics/settings | native `analyzer_cache` |
| 8 | `_test_can_reference` | dependency reanalysis | native `analyzer_cache` |
| 9 | `_test_host_bootstrap_filtering` | dependency reanalysis | native `analyzer_dependency` |
| 10 | `_test_validate_and_is_valid_agree` | diagnostics/settings | native `analyzer_cache` |
| 11 | `_test_semantic_errors` | diagnostics/settings | native `analyzer_diagnostics` |
| 12 | `_test_undeclared_identifier_diagnostic` | diagnostics/settings | native `analyzer_diagnostics` |
| 13 | `_test_review_resolution_regressions` | expressions/calls | legacy-only |
| 14 | `_test_pinned_global_api_lookup` | expressions/calls | legacy-only |
| 15 | `_test_language_utility_registry` | expressions/calls | legacy-only |
| 16 | `_test_dictionary_literal_constant_parity` | expressions/calls | legacy-only |
| 17 | `_test_unary_sign_constant_folding` | expressions/calls | native `analyzer_diagnostics` |
| 18 | `_test_analyzer_declaration_commit` | dependency reanalysis | native `analyzer_dependency` |
| 19 | `_test_declaration_head_kinds_and_conformance` | declarations | native `analyzer_dependency` |
| 20 | `_test_digest_mismatch_discards` | dependency reanalysis | native `analyzer_dependency` |
| 21 | `_test_namespace_change_invalidation` | dependency reanalysis | native `analyzer_dependency` |
| 22 | `_test_explicit_out_of_root_import` | dependency reanalysis | native `analyzer_dependency` |
| 23 | `_test_call_arity_and_types` | expressions/calls | native `analyzer_calls` |
| 24 | `_test_call_validation_methodinfo_and_signals` | expressions/calls | native `analyzer_calls` |
| 25 | `_test_named_arg_and_connect_callable` | expressions/calls | native `analyzer_calls` |
| 26 | `_test_callable_signal_constructor_and_typed_receiver_depth` | expressions/calls | native `analyzer_calls` |
| 27 | `_test_match_and_flow` | flow/finality | native `analyzer_diagnostics` |
| 28 | `_test_pinned_suite_exit_summary` | flow/finality | native #139 |
| 29 | `_test_pinned_for_assert_consumers` | flow/finality | native #139 |
| 30 | `_test_pinned_suite_datatypes` | flow/finality | native #139 |
| 31 | `_test_native_iterator_annotation_nullability` | flow/finality | native #139 |
| 32 | `_test_pinned_match_finality_domains` | flow/finality | native #139 |
| 33 | `_test_internal_type_test_exhaustion` | flow/finality | native #139 |
| 34 | `_test_pinned_match_domain_and_narrowing_audit` | flow/finality | native #139 |
| 35 | `_test_warning_settings` | diagnostics/settings | native `analyzer_diagnostics` |
| 36 | `_test_final_local_assignment` | flow/finality | native `analyzer_finality` |
| 37 | `_test_final_member_and_static_assignment` | flow/finality | native `analyzer_finality` |
| 38 | `_test_final_trait_flattening` | flow/finality | legacy-only |
| 39 | `_test_final_pattern_and_nested_expression_reads` | flow/finality | native `analyzer_diagnostics` |
| 40 | `_test_noreturn_flow` | flow/finality | native `analyzer_diagnostics` |
| 41 | `_test_unused_locals` | diagnostics/settings | native `analyzer_diagnostics` |
| 42 | `_test_unused_class_members_and_signals` | diagnostics/settings | legacy-only |
| 43 | `_test_member_name_conflicts` | declarations | native `analyzer_declarations` |
| 44 | `_test_trait_requirements_and_conformance_witness` | traits/conformance | legacy + #140 |
| 45 | `_test_flow_narrowing` | flow/finality | native `analyzer_finality` |
| 46 | `_test_lambda_capture_and_compound_narrowing` | flow/finality | legacy-only |
| 47 | `_test_get_operation_type` | expressions/calls | native `analyzer_operations` |
| 48 | `_test_builtin_annotation_resolve` | declarations | native `analyzer_declarations` |
| 49 | `_test_custom_annotation_surface` | declarations | legacy + #140 |
| 50 | `_test_type_alias_surface` | declarations | legacy-only |
| 51 | `_test_union_union_assignability` | expressions/calls | native `analyzer_type_compatibility` |
| 52 | `_test_union_store_carrier_select` | expressions/calls | native `analyzer_type_compatibility` |
| 53 | `_test_enum_case_match_and_case_binds` | expressions/calls | legacy-only |
| 54 | `_test_contextual_case_shorthand` | expressions/calls | legacy-only |
| 55 | `_test_tagged_union_match_exhaustiveness` | flow/finality | legacy-only |
| 56 | `_test_callable_bind_unbind` | expressions/calls | legacy-only |
| 57 | `_test_callable_callv_rpc` | expressions/calls | legacy-only |
| 58 | `_test_async_callable_coroutine_wrap` | expressions/calls | legacy-only |
| 59 | `_test_await_reduction_and_missing_await` | expressions/calls | legacy-only |
| 60 | `_test_coroutine_annotation_decode` | declarations | legacy-only |
| 61 | `_test_direct_async_call_wrap` | expressions/calls | legacy-only |
| 62 | `_test_surface_inheritance_member_depth` | dependency reanalysis | legacy + #140 |
| 63 | `_test_resolve_class_member_depth` | dependency reanalysis | legacy + #140 |
| 64 | `_test_foreign_member_failure_replay` | dependency reanalysis | legacy + #140 |
| 65 | `_test_foreign_class_phase_failure_replay` | dependency reanalysis | legacy + #140 |
| 66 | `_test_conformance_scoped_visibility` | traits/conformance | legacy + #140 |
| 67 | `_test_conformance_registry_registration` | traits/conformance | legacy + #140 |
| 68 | `_test_conformance_witness_lookup` | traits/conformance | legacy + #140 |
| 69 | `_test_conformance_hidden_witness` | traits/conformance | legacy + #140 |
| 70 | `_test_class_trait_binding_chain_coherence` | traits/conformance | legacy + #140 |
| 71 | `_test_recorded_trait_arguments_query` | traits/conformance | legacy + #140 |
| 72 | `_test_trait_target_assignability` | traits/conformance | legacy-only |
| 73 | `_test_witness_collision_arbitration` | traits/conformance | legacy + #140 |
| 74 | `_test_self_type_parameter_compat` | traits/conformance | legacy-only |
| 75 | `_test_enum_self_payload_field_leg` | declarations | native `analyzer_type_compatibility` |
| 76 | `_test_complete_self_referential_enum_type` | declarations | legacy-only |
| 77 | `_test_self_contract_assign_return` | expressions/calls | native `analyzer_type_compatibility` |
| 78 | `_test_self_contract_gradual_union` | expressions/calls | native `analyzer_type_compatibility` |
| 79 | `_test_local_tuple_and_literal_consumers` | expressions/calls | legacy-only |
| 80 | `_test_ordinary_assignment_and_return_consumers` | expressions/calls | native `analyzer_type_compatibility` |
| 81 | `_test_steps_1_5_repair_regressions` | expressions/calls | legacy-only |
| 82 | `_test_steps_1_5_repair2_self_signatures` | expressions/calls | legacy-only |
| 83 | `_test_local_enum_value_cycles` | declarations | legacy-only |
| 84 | `_test_concrete_cast_ternary_and_type_test_reduction` | expressions/calls | legacy-only |
| 85 | `_test_pure_literal_constant_materialization` | expressions/calls | legacy-only |
| 86 | `_test_pure_constant_review_regressions` | expressions/calls | legacy-only |
| 87 | `_test_constant_dictionary_key_conversion` | expressions/calls | legacy-only |
| 88 | `_test_nested_constant_evidence_and_contextual_casts` | expressions/calls | legacy-only |
| 89 | `_test_folded_tuple_child_and_failed_contextual_materialization` | expressions/calls | legacy-only |
| 90 | `_test_constant_producer_child_evidence` | expressions/calls | legacy-only |

## Current-main additive checkpoint

Every native case below has the same name as its legacy function without `_test_`.
Each group includes a separate `normal_reversed_shuffled_cases_restore_ambient_state`
case. The ordinary cases are independently filterable; their test functions use
`StorageFixture` and settings scopes, and conformance cases additionally scope the
conformance registry.

| Native suite | Legacy scenarios | Native cases | Preserved contract |
| --- | ---: | ---: | --- |
| `analyzer_dependency` | 10 | 11 | Exact dependency paths and source bodies, transitive invalidation, cycles, monotonic finalization, bootstrap filtering, declaration kinds/publication, stale digest rejection and explicit recovery, both changed namespaces. |
| `analyzer_diagnostics` | 8 | 9 | Semantic/M5 rejection, exact undeclared identifier, warning severity controls, unused locals/parameters, exact bool-match error, noreturn, blank-final reads, folded values and unary AST shape. |
| `analyzer_calls` | 4 | 5 | All original call, MethodInfo/signal, named-argument/connect, constructor and receiver fixtures with every validity and diagnostic predicate. |
| `analyzer_finality` | 3 | 4 | All original local/member/static finality and nullable/type-test narrowing fixtures with every predicate. |
| `analyzer_declarations` | 2 | 3 | All original member-name conflict and builtin annotation fixtures, positive/negative controls and diagnostic predicates. |
| `analyzer_operations` | 1 | 2 | Unary bool, hard binary/compound rejection, union set-wise/equality behavior and typed Array concatenation. |

The main reconciliation updates only three native flow fixture expectations: the
`body_unknown_call` and `body_failure_before_flow` controls now include main's existing
finality error, and `narrow_2948_assignment_source` uses main's nullable-specific message.
Their source strings, phases, positions, repeat checks and settings remain intact.

The main manifest's 29 suites, including every addition through PRs #186–191 and
#197–199, remain registered and unchanged. The inherited 3 migration suites plus the
6 new groups are additive. Production `BSParserRef`, current cache/declaration fixtures,
`evaluate_corpus`, and all public/corpus/triage adapters remain live.

Baseline evidence: `fbc37ca-native-baseline.log` records the full 32-suite inventory,
with only 2 stale migration cases / 6 assertions failing; every main suite passes.
The affected-group green evidence is in `simple-contracts-analyzer_*.log` under
`~/.codex/baristascript-m3/logs/155-analyzer-native-migration/`. Complete legacy/native
parity, both build systems, final public/corpus verification and post-#45 integration
remain required before removing the legacy suite.

## Remaining gates

- Migrate the 44 remaining legacy-only functions; newly merged scenarios
  must be added before any deletion.
- Give every remaining scenario a named native equivalent and verify normal plus reversed/shuffled
  execution without state leakage.
- Only then remove `analyzer_test.gd`, its UID, and analyzer-only bindings whose live corpus/public
  consumers have independently migrated. `evaluate_corpus` remains live through the corpus cutover.
- `analyzer_flow`, `analyzer_cache`, and `analyzer_type_compatibility` are required by
  `tests/native_suites.json`; keep their manifest registrations while the remaining additive
  migration proceeds.
