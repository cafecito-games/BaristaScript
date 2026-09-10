# Analyzer migration parity inventory

This inventory is grounded in `project/tests/analyzer_test.gd` at `84880e6532e5e78551692fc762f706bc44661eb9`.
Its `_init()` invokes exactly 90 unique `_test_*` functions, and every invocation has exactly one
definition. The legacy suite remains enabled in this additive checkpoint.

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

## Existing #140 native coverage

Issue #140 owns 80 already-registered native cases. They are preserved unchanged and are not
duplicated here. Their responsibility map is:

| Native suite | Cases | Responsibility | Legacy scenarios it supplements (not yet a deletion-equivalent mapping) |
| --- | ---: | --- | --- |
| `cross_file_analyzer` | 16 | imported-name precedence, ambiguity, stale providers, bootstrap and typed lookup | parser/dependency lifecycle, strict settings, declaration commit, namespace/import validation |
| `provider_analyzer` | 20 | provider ownership, member surfaces, retained generations/failures, witness and nullable heads | callable/member depth, foreign failure replay, trait/conformance and enum surface scenarios |
| `preload_analyzer` | 31 | preload identity, failure replay, recursive ownership, semantic containers | dependency cycles, explicit paths, provider failure replay, constant/container scenarios |
| `refresh_analyzer` | 13 | refresh publication, invalidation, move/remove, digest/generation and namespace repair | transitive invalidation, move/remove, declaration commit, digest mismatch, namespace change |

The exact `TEST_CASE` registrations in those four issue-owned source files remain the authoritative
#140 case-name inventory. This checkpoint neither edits nor copies them.

## Current 90-function inventory

`native #139` means this checkpoint has an explicit equivalent case above. `legacy + #140` means
#140 has related typed coverage but the legacy function still contains distinct assertions. All
other rows remain legacy-only work for continuation after #140/#141 stabilize.

| # | Invoked function | Group | Status |
| ---: | --- | --- | --- |
| 1 | `_test_parser_lifecycle` | dependency reanalysis | legacy + #140 |
| 2 | `_test_transitive_invalidation` | dependency reanalysis | legacy + #140 |
| 3 | `_test_missing_and_self` | dependency reanalysis | legacy + #140 |
| 4 | `_test_move_remove` | dependency reanalysis | legacy + #140 |
| 5 | `_test_dependency_cycle` | dependency reanalysis | legacy + #140 |
| 6 | `_test_finalization_raises_dependencies` | dependency reanalysis | legacy + #140 |
| 7 | `_test_strict_settings` | diagnostics/settings | legacy + #140 |
| 8 | `_test_can_reference` | dependency reanalysis | legacy-only |
| 9 | `_test_host_bootstrap_filtering` | dependency reanalysis | legacy + #140 |
| 10 | `_test_validate_and_is_valid_agree` | diagnostics/settings | legacy-only |
| 11 | `_test_semantic_errors` | diagnostics/settings | legacy-only |
| 12 | `_test_undeclared_identifier_diagnostic` | diagnostics/settings | legacy-only |
| 13 | `_test_review_resolution_regressions` | expressions/calls | legacy-only |
| 14 | `_test_pinned_global_api_lookup` | expressions/calls | legacy-only |
| 15 | `_test_language_utility_registry` | expressions/calls | legacy-only |
| 16 | `_test_dictionary_literal_constant_parity` | expressions/calls | legacy-only |
| 17 | `_test_unary_sign_constant_folding` | expressions/calls | legacy-only |
| 18 | `_test_analyzer_declaration_commit` | dependency reanalysis | legacy + #140 |
| 19 | `_test_declaration_head_kinds_and_conformance` | declarations | legacy + #140 |
| 20 | `_test_digest_mismatch_discards` | dependency reanalysis | legacy + #140 |
| 21 | `_test_namespace_change_invalidation` | dependency reanalysis | legacy + #140 |
| 22 | `_test_explicit_out_of_root_import` | dependency reanalysis | legacy + #140 |
| 23 | `_test_call_arity_and_types` | expressions/calls | legacy-only |
| 24 | `_test_call_validation_methodinfo_and_signals` | expressions/calls | legacy-only |
| 25 | `_test_named_arg_and_connect_callable` | expressions/calls | legacy-only |
| 26 | `_test_callable_signal_constructor_and_typed_receiver_depth` | expressions/calls | legacy + #140 |
| 27 | `_test_match_and_flow` | flow/finality | legacy-only |
| 28 | `_test_pinned_suite_exit_summary` | flow/finality | native #139 |
| 29 | `_test_pinned_for_assert_consumers` | flow/finality | native #139 |
| 30 | `_test_pinned_suite_datatypes` | flow/finality | native #139 |
| 31 | `_test_native_iterator_annotation_nullability` | flow/finality | native #139 |
| 32 | `_test_pinned_match_finality_domains` | flow/finality | native #139 |
| 33 | `_test_internal_type_test_exhaustion` | flow/finality | native #139 |
| 34 | `_test_pinned_match_domain_and_narrowing_audit` | flow/finality | native #139 |
| 35 | `_test_warning_settings` | diagnostics/settings | legacy-only |
| 36 | `_test_final_local_assignment` | flow/finality | legacy-only |
| 37 | `_test_final_member_and_static_assignment` | flow/finality | legacy-only |
| 38 | `_test_final_trait_flattening` | flow/finality | legacy-only |
| 39 | `_test_final_pattern_and_nested_expression_reads` | flow/finality | legacy-only |
| 40 | `_test_noreturn_flow` | flow/finality | legacy-only |
| 41 | `_test_unused_locals` | diagnostics/settings | legacy-only |
| 42 | `_test_unused_class_members_and_signals` | diagnostics/settings | legacy-only |
| 43 | `_test_member_name_conflicts` | declarations | legacy-only |
| 44 | `_test_trait_requirements_and_conformance_witness` | traits/conformance | legacy + #140 |
| 45 | `_test_flow_narrowing` | flow/finality | legacy-only |
| 46 | `_test_lambda_capture_and_compound_narrowing` | flow/finality | legacy-only |
| 47 | `_test_get_operation_type` | expressions/calls | legacy-only |
| 48 | `_test_builtin_annotation_resolve` | declarations | legacy-only |
| 49 | `_test_custom_annotation_surface` | declarations | legacy + #140 |
| 50 | `_test_type_alias_surface` | declarations | legacy-only |
| 51 | `_test_union_union_assignability` | expressions/calls | legacy-only |
| 52 | `_test_union_store_carrier_select` | expressions/calls | legacy-only |
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
| 75 | `_test_enum_self_payload_field_leg` | declarations | legacy-only |
| 76 | `_test_complete_self_referential_enum_type` | declarations | legacy-only |
| 77 | `_test_self_contract_assign_return` | expressions/calls | legacy-only |
| 78 | `_test_self_contract_gradual_union` | expressions/calls | legacy-only |
| 79 | `_test_local_tuple_and_literal_consumers` | expressions/calls | legacy-only |
| 80 | `_test_ordinary_assignment_and_return_consumers` | expressions/calls | legacy-only |
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

## Remaining gates

- Reconcile the 83 still-live legacy functions after #140 and #141 finish; newly merged scenarios
  must be added before any deletion.
- Give every remaining scenario a named native equivalent and verify normal plus reversed/shuffled
  execution without state leakage.
- Only then remove `analyzer_test.gd`, its UID, and analyzer-only bindings whose live corpus/public
  consumers have independently migrated. `evaluate_corpus` remains live through the corpus cutover.
- Add `analyzer_flow` to `tests/native_suites.json` atomically in the continuation phase. This
  checkpoint validates through a temporary manifest edit whose exact bytes are restored.
