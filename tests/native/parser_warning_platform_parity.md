# Parser, warning and platform native parity (#153)

Baseline: `1fe5964cd78150651a9d9a4890586ee6a5245aa2`. Every former `_test_` function maps one-to-one to the same native case name without its prefix. The parser legacy success message said 26 groups but executed 28; the map below uses executed functions. Native registration discovers `tests/native/*.cpp` in both build systems; `tests/native_suites.json` requires all four suites including tokenizer.

The migration uses typed parser snapshots (node enums and four integer coordinates), real parser instances for reuse, typed warning enums/PropertyInfo/levels, and direct compatibility shims. The legacy tree-printer idempotency check remains alongside typed node equality; precedence comparisons traverse the actual expression graph. Godot-backed values are constructed only inside running cases. The warning expectation header relocates the pre-existing independent oracle; it is not a second production warning implementation or a new expectation corpus.

## parser (28 legacy cases)

| Former function / native case | Preserved contract |
| --- | --- |
| `undecodable_source_produces_no_tree` | Raw C3 28 bytes, no tree/nodes, exactly one diagnostic; native additionally checks ERR_INVALID_DATA and exact decode text. |
| `tokenizer_diagnostic_reaches_the_parser` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `a_leading_tokenizer_diagnostic_has_a_real_position` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `a_colon_without_a_type_is_rejected` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `a_default_value_marker_without_an_expression_is_rejected` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `syntax_error_recovers_and_marks_the_tree_incomplete` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `import_before_namespace_names_the_rule` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `namespace_used_twice_is_rejected` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `global_name_kinds_are_mutually_exclusive` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `final_trait_is_rejected` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `removed_type_spelling_reports_once_from_one_definition` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `removed_type_spelling_in_every_parser_owned_type_position` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `a_valid_current_version_token_buffer_parses` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `token_buffer_lexical_and_parser_failures_are_distinct` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `token_buffer_from_another_format_is_refused` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `a_previous_format_version_buffer_is_refused` | Both original v2 binary fixtures unchanged; magic/version check and exact rejection state. |
| `a_reused_parser_clears_tokenizer_failure` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `deep_nesting_is_a_diagnostic_not_a_crash` | Same three generators using production MAX_NESTING_DEPTH+8; statement depth diagnostic still requires “too deep”. |
| `parsing_twice_is_identical` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `cold_and_cache_warm_parses_agree` | Byte-identical rich source; at least 20 kinds, complete text and both compression paths, allocation-order kinds and all four coordinates. |
| `full_span_agreement_is_sensitive_to_a_perturbed_span` | Same byte perturbation at buffer.size()-4; complete parse, unchanged node kinds, changed full spans. |
| `two_parsers_do_not_interfere` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `a_reused_parser_carries_nothing_over` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `positions_are_one_based_and_end_exclusive` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `precedence_and_associativity` | All 19 original grouping triplets, six literal/whitespace sign cases and right-associative ternary; compare typed expression operands/operators and literal values. |
| `bracketed_types_span_lines` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `node_type_vocabulary_is_closed` | Same ten sources; typed production Node::Type enum with NONE/CONFORMANCE exclusions and NODE_TYPE_MAX bound. |
| `node_type_names_are_distinct` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |

## warnings (13 legacy cases)

| Former function / native case | Preserved contract |
| --- | --- |
| `vocabulary_closure` | All 49 existing independent oracle rows moved byte-for-byte to warning_expectations.h; production enum bounds iteration, typed PropertyInfo, exact names/defaults/messages. |
| `name_round_trip_rejects_strangers` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `out_of_range_codes_resolve_to_nothing` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `absent_setting_falls_back_to_the_declared_default` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `malformed_setting_fails_loudly` | All nine malformed Variant inputs over all production codes; malformed outcome and output level unchanged. |
| `well_formed_setting_overrides_the_default` | All original inputs, acceptance/rejection assertions and diagnostic/position expectations preserved; see the identically named native case. |
| `project_settings_path_round_trip` | Absent, valid/malformed base, valid/malformed current-platform override; RAII restores both prior values/absence. CHECK assertions remain nonfatal while settings are owned. |
| `position_validation` | All 19 original spans, LF/CRLF/empty line measurements and no-lines refusal. |
| `validate_dictionary_narrows_at_the_boundary` | Production engine Dictionary boundary remains exact: five keys, lines/code/string_code and substituted cup symbol. |
| `validate_dictionary_refuses_a_position_outside_the_source` | Both past-source-line and past-line-column cases refuse emission. |
| `ordering_is_deterministic_on_one_line` | Same five warnings and five permutations; native checks complete typed comparison instead of only code sequence. |
| `ordering_is_idempotent` | Same two all-code answer passes, reverse-code input, repeated sort and ascending code order. |
| `confusable_identifier_check` | Same feature-dependent ASCII/mixed-Cyrillic controls; report which host capability branch ran. |

## platform (2 legacy cases)

| Former function / native case | Preserved contract |
| --- | --- |
| `string_builder_behavior` | Fresh, empty String/C-string, mixed append, += and final count/length/value at the same checkpoints. |
| `sname_behavior` | Same-site reference identity, both literal string values, cross-site interned equality. |

Native-only `project_settings_restore_existing_values_and_absence` repeats the settings flow in both orders in one process with absent and pre-existing base/override values. Settings scopes use CHECK assertions and RAII so destructors run after assertion failures; explicit early-return guards protect unsafe accesses.

## Adapter consumer audit

At the baseline, `rg` over tracked source/tests found `BaristaScriptParserProbe` instantiated only by parser_test.gd and analyzer_test.gd:589. The analyzer calls only `can_reference`; its adapter stays intact. The corpus uses the internal C++ parser directly in bs_corpus.cpp, with no parser-probe call. `BaristaScriptWarningRegistry` is instantiated only by warning_registry_test.gd. `BaristaScriptPlatformProbe` is instantiated only by platform_shims_test.gd. After independent parity approval, the two unconsumed classes and parser probe methods other than can_reference were removed. Production BSWarning functions and warning-table/removed-enumerator Python checks remain unchanged.

## Parity evidence

The independent reviewer approved all 43 scenario mappings and unchanged fixture/oracle identity at concurrent old/new checkpoint `98a692c1bc5c58e91829f3645c533844f28d72b0` (clean parity round 1). Only then were the original GDScript suites and unused adapters removed. At that checkpoint both SCons and CMake ran 13 tokenizer, 28 parser, 14 warnings and 2 platform cases with zero failures; both supervisor regressions passed all 16 checks, and all 13 legacy invocations printed their required sentinels. A repeated native run in reversed suite order also passed. Run stock Godot 4.7.2 `ed1daf0bf`. Legacy runs stage the ordinary debug library with the existing native supervisor staging helper, copy `scripts/` and pinned `godot-cpp/gdextension/extension_api-4-7.json` beside the disposable project, import there and run all legacy invocations with `--project` pointing to that copy. This preserves res:// paths, corpus provenance, and first-scan registration while separating user:// cache state. Native runs use the manifest supervisor against its isolated artifacts.
