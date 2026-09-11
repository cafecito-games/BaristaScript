# Cache, declaration-index and global-class parity

Immutable legacy source/fixture baseline: `1fe5964cd78150651a9d9a4890586ee6a5245aa2` (#154). The native case bodies use
production typed APIs; existing goldens and source fixtures remain read-only. Old and new suites
passed concurrently at `d5381831c62000d22fcb38e4fe8276982c65c4f4`; independent per-scenario
review approved all 41 groups before the three GDScript suites/UIDs and unused global-class
probe were retired. The immutable baseline above retains the old suites and probe sources.
Issue #21 coverage is native-only: the resurrected GDScript `project/tests/cache_test.gd` suite
was removed again so #155's native-doctest migration stays intact. Store-promotion contracts live
in `tests/native/cache_test.cpp` under `StorageFixture` with explicit prerequisite guards, including
`remove_temp_before_promotion_preserves_previous_store`,
`malformed_store_paths_reject_before_temp_creation`,
`nul_embedded_store_path_rejects_before_temp_creation` (mutable `String` NUL injection),
`first_create_replace_and_replay_byte_identity`, fault-4 absent-destination / retry / memory-payload
cases, invalid directory destination, path-shape guards, unrelated-temp preservation,
`posix_backslash_store_path_flush_round_trips` / `posix_slash_and_backslash_store_paths_are_equivalent` /
`posix_backslash_fault4_cleanup_preserves_store_and_drops_temp`
(F2/R1; non-Windows), and `windows_ascii_long_path_flush_round_trips` /
`windows_unc_store_path_flush_round_trips_with_long_paths_disabled` /
`windows_supplementary_unicode_long_path_flush_succeeds` (F1; `WINDOWS_ENABLED` only).

Each original group maps to one named native case; `repeated_and_reversed_cases_restore_ambient_state`
in each suite also executes those exact bodies twice forward and twice backward in one process.
Every execution checks a seeded ambient parser identity, source override, dependency edge,
bootstrap policy, serialized declaration index/annotation view, and scratch-directory baseline.
A hidden deliberate assertion failure exercises early-return RAII cleanup and is checked by
`tests/test_native_storage.py`; it is never in the required suite manifest.

## cache

| Legacy group | Native case in `cache_test.cpp` |
| --- | --- |
| `_test_absent_store_is_a_silent_cold_cache` | `absent_store_is_a_silent_cold_cache` |
| `_test_golden_store_from_this_build_loads` | `golden_store_from_this_build_loads` |
| `_test_golden_store_round_trips_byte_identically` | `golden_store_round_trips_byte_identically` |
| `_test_truncated_store_is_corrupt_never_absent` | `truncated_store_is_corrupt_never_absent` |
| `_test_corrupt_store_is_corrupt_never_absent` | `corrupt_store_is_corrupt_never_absent` |
| `_test_bad_magic_store_is_corrupt_never_absent` | `bad_magic_store_is_corrupt_never_absent` |
| `_test_trailing_bytes_are_corrupt_never_absent` | `trailing_bytes_are_corrupt_never_absent` |
| `_test_duplicate_key_fails_loudly` | `duplicate_key_fails_loudly` |
| `_test_duplicate_key_fails_loudly_across_versions` | `duplicate_key_fails_loudly_across_versions` |
| `_test_key_with_an_embedded_nul_is_corrupt` | `key_with_an_embedded_nul_is_corrupt` |
| `_test_version_mismatch_is_discarded_never_upgraded` | `version_mismatch_is_discarded_never_upgraded` |
| `_test_digest_mismatch_is_discarded` | `digest_mismatch_is_discarded` |
| `_test_entry_for_deleted_file_is_evicted` | `entry_for_deleted_file_is_evicted` |
| `_test_eviction_sweep_reports_every_deleted_file` | `eviction_sweep_reports_every_deleted_file` |
| `_test_warm_lookup_equals_cold_parse` | `warm_lookup_equals_cold_parse` |
| `_test_digest_is_semantic_and_path_independent` | `digest_is_semantic_and_path_independent` |
| `_test_digest_ignores_file_modification_time` | `digest_ignores_file_modification_time` |
| `_test_write_failure_is_logged_and_non_fatal` | `write_failure_is_logged_and_non_fatal` |
| `_test_atomic_write_leaves_the_previous_store_intact` | `atomic_write_leaves_the_previous_store_intact` |
| `_test_deferred_write_failure_never_replaces_the_previous_store` | `deferred_write_failure_never_replaces_the_previous_store` |
| _(issue #21)_ | `remove_temp_before_promotion_preserves_previous_store` |
| _(issue #21)_ | `malformed_store_paths_reject_before_temp_creation` |
| _(issue #21)_ | `nul_embedded_store_path_rejects_before_temp_creation` |
| _(issue #21)_ | `first_create_replace_and_replay_byte_identity` |
| _(issue #21)_ | `remove_temp_before_promotion_without_old_store_leaves_destination_absent` |
| _(issue #21)_ | `invalid_directory_destination_keeps_sentinel_and_cleans_temp` |
| _(issue #21)_ | `path_shapes_publish_with_explicit_guards` |
| _(issue #21)_ | `same_cache_retry_after_remove_temp_before_promotion_succeeds` |
| _(issue #21)_ | `failed_first_create_leaves_destination_absent` |
| _(issue #21)_ | `exact_in_memory_payload_survives_failed_promotion` |
| _(issue #21)_ | `unrelated_temp_preserved_while_owned_temp_cleaned_on_failed_promotion` |
| _(issue #21 / F2)_ | `posix_backslash_store_path_flush_round_trips` |
| _(issue #21 / F2)_ | `posix_slash_and_backslash_store_paths_are_equivalent` |
| _(issue #21 / F1)_ | `windows_ascii_long_path_flush_round_trips` |
| _(issue #21 / G1)_ | `windows_unc_store_path_flush_round_trips_with_long_paths_disabled` |
| _(issue #21 / F1)_ | `windows_supplementary_unicode_long_path_flush_succeeds` |
| `_test_miss_reason_vocabulary_is_closed` | `miss_reason_vocabulary_is_closed` |
| `_test_every_miss_reason_has_a_distinct_log_line` | `every_miss_reason_has_a_distinct_log_line` |
| `_test_source_override_shadows_the_file_on_disk` | `source_override_shadows_the_file_on_disk` |
| `_test_dependency_edges_are_recorded_and_removed` | `dependency_edges_are_recorded_and_removed` |

## declaration_index

| Legacy group | Native case in `declaration_index_test.cpp` |
| --- | --- |
| `_test_cold_and_round_trip` | `cold_and_round_trip` |
| `_test_kinds_and_views` | `kinds_and_views` |
| `_test_lifecycle_tokens` | `lifecycle_tokens` |
| `_test_commit_rejects_noncanonical` | `commit_rejects_noncanonical` |
| `_test_corrupt_fixtures` | `corrupt_fixtures` |
| `_test_atomic_write_faults` | `atomic_write_faults` |
| `_test_host_conformance` | `host_conformance` |

## global_class

| Legacy group | Native case in `global_class_test.cpp` |
| --- | --- |
| `_check_fixture_coverage` | `fixture_coverage` |
| `_check_resolutions` | `resolutions` |
| `_check_language_agrees` | `language_agrees` |
| `_check_vocabulary_closure` | `vocabulary_closure` |
| `_check_source_validity` | `source_validity` |
| `_check_qualified_name_builder` | `qualified_name_builder` |
| `_check_handled_type` | `handled_type` |
| `_check_script_surface` | `script_surface` |
| `_check_registry` | `registry` |
| `_check_class_cache` | `class_cache` |

## Preserved contracts

- Cache: all version/checksum/magic/truncation/duplicate/NUL rejection and tombstone outcomes,
  distinct miss vocabulary/logging, source-derived payload bytes, separate warm/recovery reader
  objects, identical serialization independent of store path, source/mtime handling, eviction,
  source overrides and dependency removal. AFTER_WRITE_BEFORE_RENAME leaves two distinct temps
  until asserted/cleaned; TRUNCATE_TEMP_AFTER_WRITE removes its rejected temp. Faults remain the
  existing production enum values, with #21's `REMOVE_TEMP_BEFORE_PROMOTION` (fault 4) exercising
  native replacement failure while preserving the previous store. Malformed store paths reject
  before temp creation (scratch-dir temp counts); NUL-path rejection is covered end-to-end by mutating
  an owned String via writable operator[]/ptrw(). Issue #21 promotion contracts are native-only under
  StorageFixture; no GDScript cache suite is reintroduced.
- Declaration index: same typed records, kind/view metadata, stale/current generation claims,
  canonical-path rejection, four corrupt fixture mutations and exact statuses, three write faults,
  and registered language commit/remove notifications plus installed host conformance/bootstrap
  routing. Generated candidate bytes must match the five immutable `.bsi` fixtures; no test writes
  into their directory.
- Global classes: all 25 independent seven-field fixture rows, six kind witnesses, eight source
  validity controls, five name-builder rows, nine real ResourceLoader/Script rows, 16 real ClassDB
  rows, and the editor-produced class cache with every named/nameless contribution. The manifest's
  `editor_import` list requires a real stock-Godot editor process before the native runtime process;
  no test fabricates registry/cache entries. Native-only ConfigFile bindings read that cache.

Cache golden SHA-256: `99dd5fba2e21344994b1ccf10196e2a4f686908d5eec239dedc50c7349d3a6f7` (525 bytes).
Declaration golden SHA-256: `dc3f717fe4833e7796dd131a8843c260f7ec19ca9be38d2858bad547cff3a6a3` (251 bytes).
The cache payload is the legacy deterministic nonblank/noncomment `line_index:trimmed_line`
summary, not a second parser serialization format. Warm/recovery cache checks use distinct
objects in one runtime; global discovery retains the real editor-to-runtime process boundary.

## Isolation and remaining adapters

`StorageFixture` redirects the existing debug cache/index scopes and restores the previous
bootstrap root. Its unique user-data subtree is removed on success and early return. The outer
Python supervisor also removes the full disposable project/user state on process failure or
interruption. No global settings are changed by these cases. In the pinned doctest all-asserts,
no-exceptions configuration, failed REQUIRE continues; explicit guards protect later accesses.

`BaristaScriptParseCache`, `BaristaScriptDeclarationIndexProbe`, and the production BSParserRef
registration remain because the analyzer suite still consumes cache/parser/dependency lifecycle,
source overrides, declaration synchronization/records/annotation and conformance views, bootstrap
policy, qualified-name lookup and ScriptServer bridge methods. The parse-cache surface verifier
therefore still requires its adapter in ordinary debug and excludes it in release. The ordinary
artifact audit rejects the retired global-class probe in both targets. Native helpers and
fault-test markers remain excluded from ordinary debug/release artifacts. Fixture README updates
describe the native checks; all 12 binary and 27 Barista source fixture files remain byte-identical
to the independent baseline.
