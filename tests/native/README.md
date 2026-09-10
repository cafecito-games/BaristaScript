# Native C++ tests

`TEST_SUITE("tokenizer")` / `TEST_CASE("name")` in C++ own case identities.
`tests/native_suites.json` alone declares required suites; the Python supervisor and CI
run the whole manifest by default. New `tests/native/*.cpp` files are discovered by both
build systems. Include `doctest.h` and call internal C++ APIs directly.

Do not construct Godot `String`, `Variant`, `Ref`, or other engine-backed values in static
initializers. Plain C++ case registrations are safe; Godot objects belong inside cases.
The extension registers `BaristaNativeTestRunner` only with native tests enabled. Stock
Godot selects it through `--main-loop`; the first process callback runs doctest after
engine/extension initialization. A disposable, script-free empty scene satisfies stock
Godot's project startup check. It performs no test bootstrap.

`result_protocol.h` owns the version and record prefix shared with the Python verifier.
Only completed execution emits a record with suite, exact case filter, per-run nonce,
build input identity, and executed/failed case/assertion counts. Queries emit no completion
record. The verifier requires exit 0, one matching record, positive execution counts and
zero failures. Crashes, assertion failures, timeouts, missing runners/libraries and empty
selections fail closed. The hidden `runner_failure` suite is an actual failing assertion
used only by the supervisor's subprocess tests; it is excluded from the suite manifest.
Fatal `REQUIRE` failures abort in these exception-disabled builds and are caught as failed
subprocesses; `CHECK` failures retain the usual doctest summary and completion counts.

Each run copies the fixture into a temporary directory, preserves existing `res://` paths
and first-scan extension registration, and installs only the explicitly selected test
artifact. It never imports or edits the ordinary project or its libraries. The build
records the artifact's SHA-256 and a fingerprint of its source/API/framework inputs.
SCons test objects and libraries live under `build/native-scons/`; CMake uses the selected
build directory and `native-bin/`. Normal debug/release objects exclude the framework,
runner and cases. `verify_native_surface.py` checks the resulting distribution binaries.

## Tokenizer parity checklist

Baseline: `project/tests/tokenizer_test.gd` and `src/bs_tokenizer_probe.*` at
`71d7883a02d5be5c314c761fbe4e8cfc5f8ffb2f` (#152). Native case names below are the
original test function names without `_test_`. All 24 tokenizer `.barista`/`.out` pairs
and existing buffer fixtures remain unchanged. The legacy tokenizer corpus invocation
continues to assert `BS_CORPUS 24/24 skipped=0` through the guarded GDScript runner.

| Native case | Preserved assertions |
| --- | --- |
| `positions_are_one_based_and_end_exclusive` | Three exact spans and minimum stream length. |
| `token_vocabulary_is_closed` | Original vocabulary input plus every sorted fixture; only `Empty` unreachable and never produced. |
| `buffer_round_trips_the_token_stream` | More than 200 significant sink tokens; exact replay for every source with both compression modes. |
| `tokenizing_twice_is_identical` | Same 5,000-line generator, identical complete streams and more than 5,000 tokens. |
| `tokenizers_do_not_interfere` | Fresh tokenizer instances, interleaved sink/64-line input, repeated first stream. |
| `reserved_table_is_the_only_source_of_truth` | Exactly long/uint/ulong; unique keywords, disjoint sets, exact diagnostics. |
| `removed_spellings_are_rejected_in_type_positions` | Tokenizer rejects ->/as/is; defers all four annotation positions and real parser rejects each. |
| `removed_spellings_stay_usable_as_names` | All six name positions, no diagnostic, reserved token present, no plain identifier. |
| `removal_diagnostics_name_the_spelling` | All ten suffix variants and full wording, quoted spellings/operator, ordinary invalid numeric notation. |
| `as_bang_is_not_as_followed_by_bang` | Exact joined diagnostic; spaced as/! stay separate and silent. |
| `malformed_utf8_names_the_offending_byte` | Five raw byte sequences through decode_source; exact offsets 3/0 and valid café. |
| `integer_range_is_exact` | Four exact typed integer payloads, three overflow spellings and spaced signed minimum. |
| `signed_literal_folding_boundaries` | Adjacent expression-start signs fold; spaced signs and signs after values remain operators. |

`tokenizer_helpers.*` retains the old probe's typed/tab-separated rendering and stored-literal
selection. It is a C++ utility, with no script binding. Exhausting the scan bound now explicitly
fails instead of letting equal truncated streams count as replay evidence.
