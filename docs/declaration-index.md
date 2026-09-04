# Declaration index

BaristaScript keeps a **private** declaration index beside Godot's global class
cache. Stock Godot persists a fixed key set (`class`, `language`, `path`, `base`,
`icon`, `is_abstract`, `is_tool`) and cannot carry BaristaScript declaration
kind, retroactive-conformance membership, or global-annotation declaring paths
(`docs/foundry-reuse-plan.md` §5.6, issue #44).

## Ownership

`BaristaScriptLanguage` owns one `BSDeclarationIndex`. Issue #27's concrete
`BSParserHost` delegates `get_conformance_files_in_namespace()` to that index and
must not keep a second declaration map. Issue #43 commits analyzer-produced
records after successful analysis; until then the language can synchronize a
path from the declaration-only global-class surface
(`bs_resolve_global_class_from_source`) and probes can commit richer records
(including conformance-only files) directly.

## On-disk format (BSGI v1)

Path: `res://.godot/barista_script/declaration_index.bsi` (gitignored when live).

- Magic `BSGI`, little-endian `uint32` format version (single constant
  `BSDeclarationIndex::FORMAT_VERSION`), entry count.
- Length-prefixed records **sorted by canonical `res://` path**, each with a
  64-bit record checksum; a 64-bit checksum covers the whole header/record
  sequence.
- Fields: path, source digest, namespace, optional qualified head name,
  `BSDeclarationKind`, base, abstract/tool flags, icon, sorted global
  annotations, `declares_retroactive_conformances`.
- Writes are atomic: unique sibling temp → flush → close → read-verify → rename.
  Identical logical state serializes byte-identically.

## Load / corruption

Absent file is a clean cold start. Bad magic, unsupported version, truncation,
trailing bytes, checksum failure, invalid UTF-8/path/kind, duplicate path or
qualified name, or unsorted records/lists reject the **entire** file, report a
distinct reason, and expose no partial entries.

## Writer triggers

- Successful declaration/analyzer commit for a path (generation-token guarded).
- Failed refresh removes the prior record for that path.
- Delete / rename: unconditional remove of the old path plus analyze/commit of
  the new one.
- Flush after synchronization; write failure leaves the previous valid file
  intact and does not fail source analysis.

## M4 startup contract

Exported projects have no editor filesystem scan. Before the first BaristaScript
global-name lookup at runtime, the shipped index must load and validate
successfully. Absence, corruption, or an incompatible version is fail-closed:
no trusted partial entries, no invented declarations. The M4 compiler/export
step must bind the shipped index to its compiled artifacts when sources are
unavailable. Extending the record for autoload metadata or M5 specialization
requires a format-version bump and fixtures.

## Conformance invalidation

When a commit changes a namespace's conformance-file set, the language notifies
the parser cache to invalidate every parser that reaches that named namespace
(`BSCache::collect_parsers_reaching_namespace`). The global namespace is never
treated as implicitly reached.
