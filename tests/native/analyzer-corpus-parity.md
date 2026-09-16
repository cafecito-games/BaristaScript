# Corpus per-case execution parity inventory

Native suite `analyzer_corpus` (`tests/native/analyzer_corpus_test.cpp`,
`tests/native/corpus_helpers.*`) replaces the GDScript per-case execution path:
`project/tests/corpus_runner.gd` plus the discovery, pairing, expectation gate,
comparison, failure taxonomy and guard emission of
`project/tests/corpus_harness.gd`.

Scope is the **per-case** path that `scripts/run_corpus_triage.py` supervises. The
aggregate multi-case run, `--update-expectations`, `--allow-empty`, the `BS_CORPUS`
summary line and the `_path_identity` alias machinery that exists to protect
importer-owned trees from updates stay in GDScript; they are retired by issue #156.
Rows for them are recorded at the end as deliberate non-ports.

Baseline sources are `project/tests/corpus_harness.gd` (744 lines),
`project/tests/corpus_runner.gd` (75 lines),
`project/tests/corpus_harness_test.gd` (668 lines) and
`project/tests/corpus_oracle_test.gd` (309 lines) at `7db8f3c`.

## Discovery and pairing

| GDScript behavior (file:line) | Native counterpart | Pinning test |
| --- | --- | --- |
| `.barista`/`.out` pairing by basename (`corpus_harness.gd:239-248`) | `discover_corpus` builds `expectation_path` from `get_basename() + ".out"` | `analyzer_corpus/discovery_pairs_cases_by_basename` |
| `.notest.barista` is a helper, never a case, never needs a `.out` (`corpus_harness.gd:16,240-241`) | `HELPER_SUFFIX` check before the case branch | `analyzer_corpus/notest_helper_is_never_a_case` |
| Orphaned `.out` with no `.barista` is collected separately (`corpus_harness.gd:250-255`) | `CorpusDiscovery::orphaned_expectations` | `analyzer_corpus/orphaned_expectation_is_reported_without_a_case` |
| `.baristaignore` suppresses collection and is inherited by descendants (`corpus_harness.gd:26,212-213,242-243,257-258`) | inherited `ignored` flag on the traversal stack | `analyzer_corpus/ignore_marker_is_inherited_by_descendants` |
| `.fsignore` is deliberately NOT honored (`corpus_harness.gd:19-26`) | no `.fsignore` handling exists; a tree carrying one is still collected | `analyzer_corpus/fsignore_is_not_an_ignore_marker` |
| Ignored entries increment `skipped_count`, never fail (`corpus_harness.gd:241-244`) | `CorpusDiscovery::skipped_count` | `analyzer_corpus/ignore_marker_is_inherited_by_descendants` |
| A directory `DirAccess` cannot open is recorded, never silently shrinks the corpus (`corpus_harness.gd:198-201,84-88`) | `CorpusDiscovery::unreadable_directories`; corpus mode refuses to run | `analyzer_corpus/unreadable_directory_is_recorded_not_skipped` |
| A symlinked case, expectation or subdirectory is rejected without traversal (`corpus_harness.gd:225-227`) | `is_link` check per entry before classification | `analyzer_corpus/symlinked_entries_are_rejected_without_traversal` |
| A symlinked `.baristaignore` is rejected (`corpus_harness.gd:215-216`) | marker `is_link` check | `analyzer_corpus/symlinked_entries_are_rejected_without_traversal` |
| Results are path-sorted, never dependent on directory order (`corpus_harness.gd:260-262`) | `sort_custom` on cases, `sort()` on the two string arrays | `analyzer_corpus/discovery_order_is_path_sorted` |
| Dot-prefixed entries are hidden from listing, so the marker is probed by path (`corpus_harness.gd:210-212`) | `FileAccess::file_exists` on the marker path | `analyzer_corpus/ignore_marker_is_inherited_by_descendants` |

The unreadable-directory row is pinned through the branch a failed `DirAccess::open`
takes, exercised with a directory that is not there. It is not pinned by revoking read
permission: leaving a mode-000 directory behind in the disposable `user://` root would
break the runner's own cleanup, and the open-failure branch is the same one either way.

## Expectation validity gate

| GDScript behavior (file:line) | Native counterpart | Pinning test |
| --- | --- | --- |
| Missing `.out` is `MISSING_EXPECTATION` naming the expected path (`corpus_harness.gd:275-276`) | `run_corpus_case` missing-file branch | `analyzer_corpus/case_result_shape_matches_the_gdscript_emitter` |
| Unreadable `.out` is `INVALID_EXPECTATION` (`corpus_harness.gd:278-285`) | open-failure branch | `analyzer_corpus/case_result_shape_matches_the_gdscript_emitter` |
| Round-trip-exact UTF-8 required: `text.to_utf8_buffer() == bytes` (`corpus_harness.gd:286-292`) | `check_expectation_bytes` round-trip compare | `analyzer_corpus/expectation_gate_accepts_and_rejects_exact_byte_forms` |
| No NUL byte (`corpus_harness.gd:293`) | gate NUL scan | same |
| Must end with exactly one LF (`corpus_harness.gd:293`) | `ends_with("\n")` and not `ends_with("\n\n")` | same |
| The bare `"\n"` block is rejected (`corpus_harness.gd:293`) | explicit equality check | same |
| No `\r` anywhere (`corpus_harness.gd:293`) | `contains("\r")` | same |
| Exactly one trailing LF is trimmed before comparison (`corpus_harness.gd:295`) | `trim_suffix("\n")` once | `analyzer_corpus/comparison_trims_exactly_one_trailing_newline` |

## Comparison

| GDScript behavior (file:line) | Native counterpart | Pinning test |
| --- | --- | --- |
| Byte-exact block equality, no whitespace trimming (`corpus_harness.gd:307`) | `compare_corpus_result` `!=` on whole blocks | `analyzer_corpus/paired_case_runs_end_to_end_against_its_expectation` |
| No line-ending normalization (`corpus_harness.gd:293,307`) | CR is rejected by the gate, never normalized | `analyzer_corpus/expectation_gate_accepts_and_rejects_exact_byte_forms` |
| No per-line sorting or reordering tolerance (`corpus_oracle_test.gd:64-69`) | whole-block compare | `analyzer_corpus/multi_line_block_mutations_all_mismatch` |
| Any later-line mutation, removal, addition or reorder mismatches (`corpus_oracle_test.gd:64-69`) | same | same |
| `>> ERROR at line N: message` rendering (`bs_corpus_evaluation.cpp` via `corpus_oracle_test.gd:143`) | `format_corpus_result` (already native) | `analyzer_corpus/error_block_rendering_is_pinned_whole` |
| `~~ WARNING at line N: (CODE) message` rendering (`corpus_oracle_test.gd:144-145`) | `format_corpus_result` (already native) | `analyzer_corpus/warning_block_rendering_is_pinned_whole` |
| Multi-diagnostic blocks are `\n`-joined with no trailing newline (`corpus_oracle_test.gd:143-145`) | `String("\n").join(lines)` | both rendering cases above |
| Mismatch diagnostic escaping of C0/DEL/quote/backslash (`corpus_harness.gd:393-413`) | `escape_mismatch_value` | `analyzer_corpus/mismatch_escaping_renders_visible_escapes` |
| Escaping is presentation only; `actual` carries raw bytes (`corpus_harness.gd:390-392,306,313`) | `CorpusOutcome::actual` is the raw block | `analyzer_corpus/mismatch_escaping_renders_visible_escapes` |

## Failure taxonomy and result shape

| GDScript behavior (file:line) | Native counterpart | Pinning test |
| --- | --- | --- |
| `FailureReason` ordinals 0..5 in declaration order (`corpus_harness.gd:42-49`) | `CorpusFailureReason` with explicit ordinals | `analyzer_corpus/failure_reason_ordinals_match_the_gdscript_enum` |
| Result shape check: nonempty output required (`corpus_harness.gd:333-334`) | `corpus_result_error` | `analyzer_corpus/result_shape_errors_match_the_gdscript_oracle` |
| `parser` stage must not report `analysis_ran` (`corpus_harness.gd:335`) | same | same |
| `infrastructure_error` with `ok` is contradictory (`corpus_harness.gd:335`) | same | same |
| `analyzer` stage `ok` without `analysis_ran` is contradictory (`corpus_harness.gd:335`) | same | same |
| Success sentinel without `ok`, or `parser` `ok` with a non-sentinel block (`corpus_harness.gd:339-340`) | same | same |
| Block must not contain `\r` or end with `\n` (`corpus_harness.gd:341-342`) | same | same |
| A shape error is `INVALID_RESULT` (`corpus_harness.gd:298-300`) | `run_corpus_case` | `analyzer_corpus/case_result_shape_matches_the_gdscript_emitter` |
| An unreadable source is `UNREADABLE_SOURCE`, distinct from empty input (`corpus_harness.gd:301-302,346-350`, `corpus_oracle_test.gd:106-108`) | `run_corpus_case` source read | `analyzer_corpus/unreadable_source_is_reason_three` |
| `infrastructure_error` is `INVALID_RESULT` carrying the output as the message (`corpus_harness.gd:304-305`) | `run_corpus_case` | `analyzer_corpus/infrastructure_error_is_reason_five` |
| Ordering: gate, then evaluate, then shape, then unreadable, then infrastructure, then compare (`corpus_harness.gd:273-318`) | same statement order | `analyzer_corpus/gate_precedes_evaluation` |
| Failure keys are `passed,reason,path,expectation_path,message,actual,expected` (`corpus_harness.gd:359-370`) | `corpus_case_result_dictionary` | `analyzer_corpus/case_result_shape_matches_the_gdscript_emitter` |
| `fixture_index` and `analysis_ran` are added ONLY for `OUTPUT_MISMATCH` (`corpus_harness.gd:308-317`) | same | same |
| Pass keys are `passed,path,expected,actual,analysis_ran,fixture_index` (`corpus_harness.gd:318`) | same | same |
| Gate, evaluation and comparison run as one pipeline over a committed `.barista`/`.out` pair (`corpus_harness.gd:273-318`) | `run_corpus_case` | `analyzer_corpus/paired_case_runs_end_to_end_against_its_expectation` |
| `expected` on a failure is re-read from disk and lossily decoded (`corpus_harness.gd:369`) | `FileAccess::get_file_as_string(...).trim_suffix("\n")` | `analyzer_corpus/failure_expected_field_is_reread_from_disk` |
| `fixture_index` is `{}` when the evaluation reported none (`corpus_harness.gd:315,318`) | empty `Dictionary` | `analyzer_corpus/case_result_shape_matches_the_gdscript_emitter` |
| Orphaned expectation failure record ordinal 1 (`corpus_harness.gd:122-128`) | `CORPUS_ORPHANED_EXPECTATION` ordinal, discovery-level | `analyzer_corpus/orphaned_expectation_is_reported_without_a_case` |

## Case selection, stages and fixtures

| GDScript behavior (file:line) | Native counterpart | Pinning test |
| --- | --- | --- |
| Exact case must be a valid relative `.barista` path (`corpus_harness.gd:98,533-539`) | `valid_case_relative` | `analyzer_corpus/valid_case_relative_rejects_traversal_and_helpers` |
| A helper is not selectable as an exact case (`corpus_harness_test.gd:608-611`) | `valid_case_relative` rejects `HELPER_SUFFIX` | same |
| The exact case must be discovered exactly once (`corpus_harness.gd:100-103`) | `run_selected_corpus_case` selection | `analyzer_corpus/selected_corpus_case_emits_guards` (corpus mode) |
| Stage manifest shape: exactly 3 keys, `schema_version` integer 1, matching `foundry_revision`, `cases` object (`corpus_harness.gd:502-503`) | `validate_stage_manifest` | `analyzer_corpus/stage_manifest_shape_is_validated` |
| Every manifest key is a valid relative case with stage `parser`/`analyzer` (`corpus_harness.gd:511-513`) | same | same |
| Every discovered case has an entry; no extra or helper entries remain (`corpus_harness.gd:514-521`) | same | same |
| Discovery errors or unreadable directories abort manifest validation (`corpus_harness.gd:504-508`) | same | `analyzer_corpus/stage_manifest_shape_is_validated` |
| Strict JSON: grammar, duplicate decoded keys, integer `schema_version` (`corpus_harness.gd:605-729`) | `StrictJsonValidator` / `read_unique_json` | `analyzer_corpus/strict_json_matrix_matches_the_shared_fixture` |
| Non-UTF-8 or NUL-bearing JSON is rejected (`corpus_harness.gd:718-720`) | `read_unique_json` | same |
| Fixture sources are collected recursively and sorted strictly ascending (`corpus_harness.gd:472-475,733-744`) | `fixture_source_paths` plus a final sort | `analyzer_corpus/fixture_source_paths_are_sorted_and_barista_only` |
| Analyzer staging pulls staging plus `corpus/parser` plus `corpus_support/parser` (`corpus_harness.gd:461-475`) | `staging_fixture_paths` | same |

## Guard emission

| GDScript behavior (file:line) | Native counterpart | Pinning test |
| --- | --- | --- |
| `BS_CASE_RESULT <json>` is printed before `BS_CASE_RAN <relative>` (`corpus_runner.gd:65-68`) | `emit_corpus_guards` | `analyzer_corpus/guard_lines_are_ordered_result_then_ran` |
| The payload is `JSON.stringify` of the result dictionary (`corpus_runner.gd:66`) | `godot::JSON::stringify` | same |
| `BS_CASE_RAN` carries the relative case exactly as selected (`corpus_runner.gd:67`) | `corpus_case()` verbatim | same |
| A failing case still prints both guards; `BS_CASE_RAN` means "ran", not "passed" (`corpus_runner.gd:63-68`, `run_corpus_triage.py:152`) | guards emitted regardless of `passed` | `analyzer_corpus/guard_lines_are_ordered_result_then_ran` |
| `path` is the corpus root joined with the relative case, which the supervisor re-derives (`run_corpus_triage.py:145`) | `CorpusCase::path` | same |

## Deliberate non-ports (issue #156 owns these)

| GDScript behavior (file:line) | Disposition |
| --- | --- |
| Aggregate multi-case run and `FAIL ...` output lines (`corpus_harness.gd:105-119`) | out of scope; GDScript retains it |
| `BS_CORPUS <passed>/<total> skipped=<n>` summary (`corpus_harness.gd:416-417`) | out of scope; still emitted by the GDScript runner the supervisor invokes |
| `--update-expectations` and its refusal rules (`corpus_harness.gd:137-160,373-387`) | out of scope |
| `--allow-empty` (`corpus_harness.gd:131-135`) | out of scope |
| `ExitCode` vocabulary and `error_result` (`corpus_harness.gd:36-40,175-179`) | out of scope; native suites report through doctest and the native result record |
| `_path_identity` / `_resolve_filesystem_path` alias resolution (`corpus_harness.gd:542-599`) | out of scope: it exists to stop `--update-expectations` writing through an alias into an importer-owned tree. The native path normalizes with `ProjectSettings::localize_path` and rejects a symlinked root outright, which is fail-closed for a read-only run. |
| Imported-root ownership lookup through `scripts/corpus_sources.json` (`corpus_harness.gd:437-475`) | partially ported: the registry is read only for `revision`, because the remaining branches exist to refuse updates |
| `fixture_stages` explicit local-fixture stage selection (`corpus_harness.gd:53,477-493`) | out of scope: native corpus mode always reads `case_stages.json` at the corpus root |
