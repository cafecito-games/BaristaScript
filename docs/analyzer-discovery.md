# Analyzer discovery and adjudication (issue #45, checkpoints A–B)

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

Checkpoint B preserves the complete 1,346-case inventory while materializing
264 source-reviewed eligibility decisions. Exactly 231 static cases whose
assertions require M5 generics are deferred alongside the four exact
compiler/runtime cases from checkpoint A. Exactly 33 whole cases are excluded:
32 assert only integer widths or signedness removed by BaristaScript's D1
single-carrier contract, and one requires a Foundry-engine-only native surface
that stock Godot cannot provide. These decisions come from pinned source and
dependency review, never from whether the current analyzer happens to pass.

The resulting staging equation is `1,346 = 1,078 included + 33 excluded + 235
deferred`. Included cases split into 662 errors, 354 features, and 62 warnings;
their pinned statuses are 646 analyzer errors, 414 successes, and 18 parser
errors. All 250 analyzer helpers and the two auxiliary support identities remain
in the inventory and available to selected cases. No included case may refer to
an excluded or deferred paired case. Concrete M3 adaptations and retained or
mixed projections remain included until their later, independently reviewed
packets; this policy change does not rewrite their sources or expectations.

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
failed records. `--jobs N` executes N cases concurrently, each in its own
staged project so no two case processes share writable engine state, and
records the requested count; the default of one case at a time is unchanged.
Results stay ordered by case and the report is still rewritten after every
completion, whatever order the cases finish in. Every staged project is built
and validated before the first case runs, so N is a fixed startup cost of N
copies of `project/`; around `os.cpu_count()` is the useful range, and a value
far above the host's parallelism pays that cost for no throughput.

A failed case stops the run before any further case starts, and an interruption
kills the case processes still running rather than waiting out their timeouts. A
killed process yields truncated output, so it produces no result and is named in
`stopped_cases` instead; a case still in flight that finishes on its own keeps
its record. Selected cases in neither list were never dispatched.

A nonzero complete report is useful discovery evidence, not a
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

The adjudicated policy records 1,078 static observations as
`execution_reviewed`, 231 static observations plus the four exact later-stage
cases as `source_reviewed_deferred`, and 33 reviewed exclusions as
`source_reviewed_excluded`. The importer enforces both directions of these
owner/disposition relationships and fails closed on missing, cross-labelled, or
orphaned owner state. Each static owner has a source assertion, bounded
code family, prerequisites, and hashes of its original full report, source,
expectation and complete expected/actual blocks. Six disjoint source reviews
cover all 1,346 pairs and their identified providers. Source scope observations
remain distinct from actual execution and from checkpoint B eligibility.

The complete frozen run at `13983c45b63b9dbe9da5b72f959814f19aaad1ae`
produced 372 passes, 966 mismatches and four crashes. Two exact language identity
corrections were separately executed at
`fa6380c0602883310f82176de7eb7909a0a1139b`: the registered `BaristaScript`
resource type in `features/use_preload_script_as_type`, and the canonical
language name in `errors/retroactive_conformance_enum_target`. Both still have
guarded semantic mismatches. Their replacement observations retain their own
execution revision and hashes; no later metadata commit claims a new full run.

The enum cycle crashes belong to #138 local enum constant/member resolution.
The trait metatype assignment crash is assigned by its closed compatibility
assertion to #138 with #141 metadata; the builtin-conformance crash belongs to
#141 with #138 and separately preserved M5 bound checking. Only the enum pair
has a symbolic crash-origin diagnosis. No crash is a pass or a missing-source
exemption. ScriptRunner's engine-only hook and the closed special `create_proxy`
assertions stay visible for #141 S7 and #45 stock-surface disposition review.
The original five-line `match_with_subscript` fixture retains its line-seven
oracle: pinned null-origin errors use the parser's previous-token fallback.
