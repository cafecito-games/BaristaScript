# Static oracle miniature controls

These files exercise #31 checkpoint A. They do not restore the parser corpus's four
analyzer debts or its 29 warning cases, and are not an imported analyzer corpus.

`producer/*.upstream` and `*.source` are byte-for-byte Git blobs at Foundry
`c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6`. `producer/provenance.json` records the
paths and SHA-256 hashes. The adjacent `.block` files were derived independently
from those pinned outputs: remove the status; for success keep only the initial
warning prefix, or use the shared success sentinel. Runtime output is excluded.
The producer is `modules/foundry_script/tests/fs_test_runner.cpp:1007–1055`.

`corpus_oracle_test.gd` passes the exact unused-variable and leading-number-separator
sources to the real raw analyzer oracle. Its parser-warning control retains the
first function of `warnings/confusable_identifier.fs`, omits comments and the
unrelated GET_NODE function, and preserves the Greek-identifier line 5 and its
pinned warning. Clean and annotation-ignore controls assert the shared sentinel;
the latter uses the already-supported unused-signal annotation contract. The
parser rejection reuses the existing tokenizer fixture's committed diagnostic.
`frontend/` contains those independent source/block pairs plus a clean control; the
real stage-aware harness runs all four at analyzer stage. No `.barista` function runs.

The other blocks exercise extraction and the actual harness comparator with
explicit injected results. Their passing transport assertions are not evidence
that the corresponding upstream semantic cases pass BaristaScript analysis.
Synthetic native records separately test source-order error ties, warning list
order, error/warning exclusion, and failure without diagnostics. Local fixture
updates are tested only in a disposable `user://` directory.
