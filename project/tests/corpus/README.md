The conformance corpora, one directory per pipeline stage.

- `parser/` — 340 cases imported from Foundry by `scripts/import_parser_corpus.py`; see its own
  README for provenance, counts and the cases M3 has to revisit. Population accounting (upstream
  344 runnable + 2 helpers, 16 D1/hard-fork dispositions) lives in the triage ledger inside
  `tests/corpus_baseline.json`, not in a grep hit count.
- `analyzer/` — pending import (#45). The ledger already scaffolds the pinned upstream enumeration
  (1,596 / 1,346 / 250); final imported totals are written only after execution-driven triage.

A corpus is run by `corpus_runner.gd` and pinned by an anchored summary line in
`tests/gdscript_suites.json`, which `tests/validate_ci.py` derives from `tests/corpus_baseline.json`
and refuses to let drift. Later stages (runtime, completion, LSP, refactor) land in their
own subdirectories in later milestones.
