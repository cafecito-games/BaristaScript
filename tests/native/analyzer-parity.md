# Analyzer migration parity inventory

This inventory is reconciled with `project/tests/analyzer_test.gd` at merged main
`4b2439fb95a4ed6069c3771ea1663ae50eeb8ac1` (PR #199), rechecked unchanged at
`eb0e7275747f6ac585117777f17222c70b66f30e` (PR #200), and reconciled with the two
Callable-member changes at `4c9c561c6096e020440483328c13fc0fcbd3aa71` (PR #201).
The suite still invokes exactly
90 unique scenarios. A normal merge at `fbc37cad2a4aa2e20694863f8b79106810617379`
preserves the original migration commits `9c5e203`, `19085cd`, and `14343b9`.

The legacy suite remains enabled: 86 scenarios have explicit native equivalents and 4
still require migration. No analyzer, cache, index, public, corpus, or triage adapter
has been removed. Counts in the historical checkpoint sections describe those checkpoints;
current execution evidence is recorded separately below.

## Additive #139 native foundation

The seven functions added after the issue's `71d7883a` baseline are registered as seven filterable
cases in native suite `analyzer_flow`. They use `BSParser`, `BSAnalyzer`, `BSWarning`, typed AST
nodes, `StorageFixture`, and fail-closed helper guards. The non-source-reachable exhaustion
control uses the `BARISTA_TESTS`-only `AnalyzerMigrationTestAccess` friend and typed observations;
its carrier compatibility assertions call `BSTypeCompatibility` directly. No #155-authored
native case depends on the analyzer script probe; existing main-suite consumers remain intact.

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

At that historical checkpoint, `_test_complete_self_referential_enum_type` and
`_test_local_tuple_and_literal_consumers` remained legacy-only. The former now has a typed
`analyzer_conformance` equivalent; the latter is still pending.

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
| 13 | `_test_review_resolution_regressions` | expressions/calls | native `analyzer_resolution` |
| 14 | `_test_pinned_global_api_lookup` | expressions/calls | native `analyzer_resolution` |
| 15 | `_test_language_utility_registry` | expressions/calls | native `analyzer_resolution` |
| 16 | `_test_dictionary_literal_constant_parity` | expressions/calls | native `analyzer_constants` |
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
| 38 | `_test_final_trait_flattening` | flow/finality | native `analyzer_finality` |
| 39 | `_test_final_pattern_and_nested_expression_reads` | flow/finality | native `analyzer_diagnostics` |
| 40 | `_test_noreturn_flow` | flow/finality | native `analyzer_diagnostics` |
| 41 | `_test_unused_locals` | diagnostics/settings | native `analyzer_diagnostics` |
| 42 | `_test_unused_class_members_and_signals` | diagnostics/settings | native `analyzer_diagnostics` |
| 43 | `_test_member_name_conflicts` | declarations | native `analyzer_declarations` |
| 44 | `_test_trait_requirements_and_conformance_witness` | traits/conformance | native `analyzer_conformance` |
| 45 | `_test_flow_narrowing` | flow/finality | native `analyzer_finality` |
| 46 | `_test_lambda_capture_and_compound_narrowing` | flow/finality | native `analyzer_finality` |
| 47 | `_test_get_operation_type` | expressions/calls | native `analyzer_operations` |
| 48 | `_test_builtin_annotation_resolve` | declarations | native `analyzer_declarations` |
| 49 | `_test_custom_annotation_surface` | declarations | native `analyzer_declarations` |
| 50 | `_test_type_alias_surface` | declarations | native `analyzer_declarations` |
| 51 | `_test_union_union_assignability` | expressions/calls | native `analyzer_type_compatibility` |
| 52 | `_test_union_store_carrier_select` | expressions/calls | native `analyzer_type_compatibility` |
| 53 | `_test_enum_case_match_and_case_binds` | expressions/calls | native `analyzer_enum` |
| 54 | `_test_contextual_case_shorthand` | expressions/calls | native `analyzer_enum` |
| 55 | `_test_tagged_union_match_exhaustiveness` | flow/finality | native `analyzer_enum` |
| 56 | `_test_callable_bind_unbind` | expressions/calls | native `analyzer_calls` |
| 57 | `_test_callable_callv_rpc` | expressions/calls | native `analyzer_calls` |
| 58 | `_test_async_callable_coroutine_wrap` | expressions/calls | native `analyzer_calls` |
| 59 | `_test_await_reduction_and_missing_await` | expressions/calls | native `analyzer_calls` |
| 60 | `_test_coroutine_annotation_decode` | declarations | native `analyzer_calls` |
| 61 | `_test_direct_async_call_wrap` | expressions/calls | native `analyzer_calls` |
| 62 | `_test_surface_inheritance_member_depth` | dependency reanalysis | native `analyzer_members` |
| 63 | `_test_resolve_class_member_depth` | dependency reanalysis | native `analyzer_members` |
| 64 | `_test_foreign_member_failure_replay` | dependency reanalysis | native `analyzer_members` |
| 65 | `_test_foreign_class_phase_failure_replay` | dependency reanalysis | native `analyzer_members` |
| 66 | `_test_conformance_scoped_visibility` | traits/conformance | native `analyzer_conformance` |
| 67 | `_test_conformance_registry_registration` | traits/conformance | native `analyzer_conformance` |
| 68 | `_test_conformance_witness_lookup` | traits/conformance | native `analyzer_conformance` |
| 69 | `_test_conformance_hidden_witness` | traits/conformance | native `analyzer_conformance` |
| 70 | `_test_class_trait_binding_chain_coherence` | traits/conformance | native `analyzer_conformance` |
| 71 | `_test_recorded_trait_arguments_query` | traits/conformance | native `analyzer_conformance` |
| 72 | `_test_trait_target_assignability` | traits/conformance | native `analyzer_conformance` |
| 73 | `_test_witness_collision_arbitration` | traits/conformance | native `analyzer_conformance` |
| 74 | `_test_self_type_parameter_compat` | traits/conformance | native `analyzer_conformance` |
| 75 | `_test_enum_self_payload_field_leg` | declarations | native `analyzer_type_compatibility` |
| 76 | `_test_complete_self_referential_enum_type` | declarations | native `analyzer_conformance` |
| 77 | `_test_self_contract_assign_return` | expressions/calls | native `analyzer_type_compatibility` |
| 78 | `_test_self_contract_gradual_union` | expressions/calls | native `analyzer_type_compatibility` |
| 79 | `_test_local_tuple_and_literal_consumers` | expressions/calls | legacy-only |
| 80 | `_test_ordinary_assignment_and_return_consumers` | expressions/calls | native `analyzer_type_compatibility` |
| 81 | `_test_steps_1_5_repair_regressions` | expressions/calls | native `analyzer_calls` |
| 82 | `_test_steps_1_5_repair2_self_signatures` | expressions/calls | native `analyzer_calls` |
| 83 | `_test_local_enum_value_cycles` | declarations | native `analyzer_declarations` |
| 84 | `_test_concrete_cast_ternary_and_type_test_reduction` | expressions/calls | legacy-only |
| 85 | `_test_pure_literal_constant_materialization` | expressions/calls | legacy-only |
| 86 | `_test_pure_constant_review_regressions` | expressions/calls | native `analyzer_constants` |
| 87 | `_test_constant_dictionary_key_conversion` | expressions/calls | native `analyzer_constants` |
| 88 | `_test_nested_constant_evidence_and_contextual_casts` | expressions/calls | native `analyzer_constants` |
| 89 | `_test_folded_tuple_child_and_failed_contextual_materialization` | expressions/calls | legacy-only |
| 90 | `_test_constant_producer_child_evidence` | expressions/calls | native `analyzer_constants` |

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

## Typed conformance, callable, warning and finality checkpoint

Nineteen more legacy scenarios have same-name cases in the inventory above. All original
source fixtures and predicates are retained: nine conformance scenarios (including private
visibility/registry/enum operations), six callable/async scenarios, trait-final flattening,
lambda capture plus all 64 fresh-parser assignment repetitions, type aliases, and unused
class members/signals. The conformance access layer replaces only the old script Dictionary
carrier with typed C++ observations and is gated by `BARISTA_TESTS`; no public binding is added.
Public `_validate` checks call the actual language API, not a probe serializer. The direct-async
case explicitly enables the MISSING_AWAIT profile its legacy predecessor supplied implicitly.

Each added scenario runs separately and in normal, reverse and deterministic shuffled order
through scoped cache/index/files, conformance registry and analyzer settings. Private exhaustion
controls now use typed fields/errors and compare two complete observations for repeatability.
The complete SCons supervisor log `65-parity-native-full.log` records 39 required suites,
817 executed cases and 65,637 assertions, with zero failed cases/assertions. The legacy suite
and all adapters remain unchanged; this is progress evidence, not deletion authorization.

## Enum, projected argument and annotation checkpoint

Three full legacy enum scenarios now have same-name cases in `analyzer_enum`. Typed
trait-target observations preserve all 17 membership/compatibility predicates and all 70
named tri-state projection controls, including complete repeat equality, without a Dictionary
observer. Custom annotation cases preserve the full producer sources, seven rejection controls,
unknown-name rejection, imported signatures and retained dependency phase. All five scenarios
join normal/reverse/shuffle runs. The suite-exit flow group now also checks raw diagnostic queue
order on both runs, including the two current-main raw-order overrides.

The last fully integrated checkpoint, `a0988d8`, normally merged PR #200 and passed SCons and
CMake inventories (40 suites, 828 cases, 67,137 assertions each), ordinary current build identity
and native surface checks, all eight public/corpus sentinel runs, CI configuration, global API,
language utilities, format, license and diff checks. Its evidence is `a0988d8-*`; newer focused
evidence is `enum-trait-stage-*` and `annotation-raw-stage-*`. No legacy coverage or adapters
were deleted.

## Remaining gates

`language_utility_registry` now checks all eleven utility signatures across their original
contexts, public MethodInfo round-trip fields, non-executable private callable identities,
constant flags and results, overload/context failures, and exact unknown-name diagnostics.
It preserves the PR #201 `len.call` member-positive fixture. Utility-bearing constant
exportability is observed directly through typed AST/Variant carriers without a script probe.
`language-utility-stage-1.log` records four cases / 13,561 assertions with zero failures,
including normal/reverse/shuffle runs; no semantic production repair was required.

The native `pinned_global_api_lookup` case reads the exact selected pinned producer JSON from
the legacy `res://../godot-cpp/gdextension` location. Build configuration selects only its filename;
no generated analyzer metadata supplies expected names or values. Every global constant/enum
value, both exact int64 bounds, every utility value, and both indexed/imported handles retain
their legacy predicates. The supervisor copies only this JSON into its disposable tree. Three
focused staging tests cover exact selection/bytes/presence, missing input without fallback, and
copy-failure cleanup; they failed before implementation and passed afterward. The real-process
supervisor passes all 19 tests. `pinned-api-stage-1.log` records three cases / 11,578 assertions
with zero failures, including normal/reverse/shuffle runs.

Normal merge `c990405` integrates PR #201 without rewriting the original commits. Both the
private `reduce_subscript` signature and the test-only analyzer friend remain present; every
new main suite is retained. `merge201-native-full.log` records 43 suites, 850 cases and 72,756
assertions with zero failures at clean `c990405`. The new `analyzer_resolution` scenario keeps
all 53 resolution-regression fixture invocations, including the exact PR #201 Callable-member
negative. `resolution-stage-2.log` records two cases / 439 assertions with zero failures,
including normal/reverse/shuffle runs. Its first build exposed an ambiguous Godot String
concatenation, corrected using explicit String construction without changing fixture bytes.

The first `analyzer_constants` cases preserve both dictionary-literal and typed-key conversion
functions: exact nested key/value carriers, empty and runtime dictionaries, duplicate-key
messages/positions, six builtin conversion positives and four exact rejected-key controls.
`constants-stage-2.log` records three cases / 722 assertions with zero failures, including
normal/reverse/shuffle isolation.

The third constants scenario preserves all 27 pure-constant review fixtures: read-only converted
carriers, typed selections, exact public consumer/subscript diagnostics, packed selections, and
runtime-child refusal. `constants-stage-3.log` records four cases / 1,617 assertions with zero
failures, including normal/reverse/shuffle isolation.

The next additive group covers four complete same-/cross-file member lookup and owner-failure
replay scenarios in `analyzer_members`, Self parameter compatibility in `analyzer_conformance`,
and both exact local enum-cycle diagnostic blocks plus the recursive tagged-payload positive
control in `analyzer_declarations`. All six join the normal/reverse/shuffle isolation runs.
Focused results are recorded in `members-stage-analyzer_*.log`.

The Self-signature repair scenario preserves all 12 public source validations and all 20 ordered
message/line/column diagnostics, including fixed/rest receiver identity, inherited constructors,
Callable fixed/return/rest slots, and typed-tail positive/negative consumer controls.
`self-signatures-stage-1.log` records `analyzer_calls`: 12 cases / 1,733 assertions, zero failures,
including normal/reverse/shuffle execution. No private probe carrier is used.

At clean `5234637` (82 scenarios), `parity82-scons-full.log` and `parity82-cmake-full.log` each
record 44 suites / 854 cases / 86,317 assertions with zero failures and actual completion records.
Both supervisors pass 19 tests. The public/corpus runner retains all eight invocations, including
`BS_SUITE_OK analyzer_test`, parser 340/340 (2 skipped), and tokenizer 24/24 (0 skipped).
Current ordinary identity/surface, editor import, CI, global API, language utilities, format,
license, and diff checks also pass. Final integration must repeat these gates after #45 merges.

The preceding repair-regressions scenario additionally preserves 25 public validations,
45 ordered exact diagnostics, the strict-dynamic toggle, and both gradual tuple safe-line
observations. It joins the normal/reverse/shuffle isolation runner without changing semantics.

All 41 constant-producer controls now use direct typed AST observations and recursive carrier
checks (exact scalar type/value, child order, and read-only containers), preserving validity,
hard types, partial/failed materialization, complete public errors/warnings, semantic validity,
and exact repeated public reports. `producer-stage-1.log` records 5 cases / 4,320 assertions,
zero failures, including normal/reverse/shuffle execution.

Nested constant evidence preserves 11 established-child and 16 nullable/converted-child sources,
three exact raw-key refusals, six contextual casts, and the runtime nonconstant control. Direct
typed values retain integer/float distinctions and recursive read-only evidence.
`nested-constants-stage-1.log` records 6 cases / 6,415 assertions, zero failures, including
normal/reverse/shuffle execution.

- Migrate the 4 remaining legacy-only functions; newly merged scenarios
  must be added before any deletion.
- Give every remaining scenario a named native equivalent and verify normal plus reversed/shuffled
  execution without state leakage.
- Only then remove `analyzer_test.gd`, its UID, and analyzer-only bindings whose live corpus/public
  consumers have independently migrated. `evaluate_corpus` remains live through the corpus cutover.
- `analyzer_flow`, `analyzer_cache`, and `analyzer_type_compatibility` are required by
  `tests/native_suites.json`; keep their manifest registrations while the remaining additive
  migration proceeds.
