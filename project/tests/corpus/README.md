The conformance corpora, one directory per pipeline stage.

- `parser/` — 340 cases imported from Foundry by `scripts/import_parser_corpus.py`; see its own
  README for provenance, counts and the cases M3 has to revisit.

A corpus is run by `corpus_runner.gd` and pinned by an anchored summary line in
`tests/gdscript_suites.json`, which `tests/validate_ci.py` derives from `tests/corpus_baseline.json`
and refuses to let drift. Later stages (analyzer, runtime, completion, LSP, refactor) land in their
own subdirectories in later milestones.
