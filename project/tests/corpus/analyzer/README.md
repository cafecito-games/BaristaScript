# Analyzer conformance corpus

Imported from Foundry. 1078 static cases, 252 helpers.
Foundry `c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6`. Full upstream inventory: 1346 cases, 250 analyzer helpers; 2 support identities, 1 supplied by the existing external parser-owned delivery.
Generated helper projections: 1.
Residual failures pinned in `tests/corpus_baseline.json`: 168.
Cases are supervised by `scripts/run_corpus_triage.py`, in one process for the whole corpus by
default and one process per case under `--execution isolated`; every included dependency remains
available. No script body executes.
