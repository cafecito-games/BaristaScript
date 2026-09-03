Fixture corpus for the fail-closed contract of the corpus harness.

Every directory is one row of the fail-closed table in issue #5. These
fixtures are NOT conformance cases: they exist so corpus_harness_test.gd
can assert each failure mode by name. A plain run over this tree is
expected to exit 1.

- passing/                    paired case + a .notest.barista helper (skipped, not counted)
- ignored_directory/          .baristaignore opts the directory out; its case is skipped
- missing_expectation/        .barista with no .out
- orphaned_expectation/       .out with no .barista
- invalid_expectation/        .out that is not valid UTF-8
- parse_failure_vs_ok/        source fails to parse while the .out says BS_TEST_OK
- trailing_whitespace_drift/  expectation differs from actual output by one trailing space
- line_ending_drift/          expectation uses CRLF while actual output uses LF
- empty_corpus/               no cases at all
