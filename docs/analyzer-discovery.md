# Analyzer discovery (issue #45, checkpoint A)

The analyzer is pending: `corpora.analyzer.imported=false`, and
`project/tests/corpus/analyzer` must remain absent. Discovery is an inventory and
an execution report, not a passing M3 analyzer baseline. The parser and default
aggregate remain pinned to 340/340, with two skipped parser helpers.

The shared `scripts/corpus_sources.json` fixes the Foundry repository/revision,
parser/analyzer roots, and two exact auxiliary helpers. CI acquires those inputs
with anchored non-cone sparse patterns. Verification compares the cached index
against the pin, enumerates every consumed file (including ignored/untracked
files), and hashes raw bytes against pinned Git blobs. It needs neither an
upstream engine build nor unselected root ignore/attribute blobs.

`scripts/analyzer_corpus_policy.json` is the only authored analyzer policy.
The complete inventory validates 1,596 analyzer sources: 1,346 source/output
pairs and 250 `.notest` helpers. Category/status totals are checked before any
disposition subtraction. The Utils and RtcSelfAdoptable helpers are separately
counted; all 252 physical helpers stay available to selected cases. Source and
expectation edits carry exact pin-bound hashes, byte spans, preimages, line
anchors, rules and occurrence counts. Ordinary strings, comments, namespace
identities, and runtime transcript are not rewritten.

Only the four exact compiler/runtime-error cases are deferred in discovery.
Generic and hard-fork scope observations are review candidates; they do not
silently remove cases. Final reviewed dispositions, concrete adaptations,
passing analyzer/aggregate pins, and promotion belong to checkpoint B after
#138–#141 and #31 restoration.

Run from the repository root with a clean checkout of the registered source:

```sh
python3 scripts/import_analyzer_corpus.py --foundry /path/to/Foundry --revision c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6 --inventory /tmp/analyzer-inventory.json
python3 scripts/import_analyzer_corpus.py --foundry /path/to/Foundry --revision c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6 --stage project/tests/corpus_staging/analyzer
scons api_version=4.7 target=template_debug
godot --headless --path project --editor --quit
python3 scripts/run_corpus_triage.py --godot /path/to/godot --corpus res://tests/corpus_staging/analyzer --report /tmp/analyzer-results.json
```

The staging destination is importer-owned and outside aggregate corpus
discovery. Generation validates into temporary storage before replacing that
destination, restoring the previous tree if publication fails. `--check` is
read-only: it validates the complete source inventory and pending delivery
contract, and checks the staging bytes when staging exists. It never claims an
imported passing tree. `--inventory` writes only the requested external report.

For one exact reproducer append, for example,
`--case errors/variant_constant_known_value_rejected.barista`. Repeated `--case`
arguments preserve the requested order for comparisons across separate processes (duplicates are
rejected; A→B→A restoration is checked in-process by the oracle suite). Selection controls execution, not dependency availability.
Every case is analyzer-stage, including upstream parser errors, which stop
before analysis. The debug raw probe populates the existing scoped declaration
index from production parser/head APIs, retaining full namespace identities,
annotation-only and conformance-only declarations. It does not register flat
Godot global classes, resolve dependencies in Python, or execute `.barista`
bodies. Each result includes the actual temporary fixture-index counts.

Triage starts a fresh Godot process for each case, with a finite 30-second
limit; `--timeout SECONDS` explicitly overrides and records it. It requires the
exact `BS_CASE_RAN` guard, one machine-readable result, the aggregate guard,
full block equality, and successful exit. Crashes, timeouts, malformed results,
missing guards, infrastructure errors, and semantic mismatches remain distinct
failed records. A nonzero complete report is useful discovery evidence, not a
passing baseline. Reports record source/expectation hashes, source pin,
BaristaScript revision and source-file hashes, debug-library hashes, official
engine version, full expected/actual blocks, owner/prerequisite observations,
commands and terminal conditions. Execution never rewrites policy or goldens.

Remove only the owned staging output before the final registered suite and
provenance gates. Do not commit that generated tree or change the pending
baseline to absorb a selected passing subset. Offline miniature regression
fixtures retain pinned bytes and hashes in
`tests/fixtures/analyzer_import/provenance.json`; `--foundry` additionally runs
the full real producer, same-count drift and removed-case checks.
