# Runtime importer inputs

A byte-pinned miniature of the Foundry runtime importer input. The original `.fs` and
`.out` paths and contents are retained, so the tests exercise the real `.fs` to `.barista`
conversion, the full-transcript expectation and the cross-corpus reference with exact
source provenance. `errors/reload_suspended_function.out` is the helper-owned expectation
that must never become a case.

Generated output belongs under `project/tests/corpus/runtime`, the registered destination.
