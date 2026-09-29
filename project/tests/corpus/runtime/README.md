# Runtime conformance corpus

Imported from Foundry. 394 runtime cases, 82 helpers.
Foundry `c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6`. Full upstream inventory: 839 sources, 757 runnable cases, 82 helpers and 758 expectations; 1 expectation belongs to a helper rather than a case and is recorded in `inventory.json` instead of becoming one.
Expected status over the 757 runnable cases: FS_TEST_ANALYZER_ERROR 10, FS_TEST_OK 482, FS_TEST_RUNTIME_ERROR 265.
Population: 757 = 394 imported + 65 excluded + 298 deferred.
Residual failures pinned in `tests/corpus_baseline.json`: 326.
A case expectation is the complete upstream transcript, relocated onto this destination and
otherwise byte-identical. `scripts/run_corpus_triage.py` compiles and runs each case against it;
every case that still fails is pinned with a reason naming the child that owns it, and the pin
shrinks only through this importer reading a completed execution report.
