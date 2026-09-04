# Declaration index (BSGI)

BaristaScript's private declaration index lives at
`res://.godot/barista_script/declaration_index.bsi`. Live files under that path are
gitignored; only deliberately generated fixtures in this directory are checked in.

## Format (v1)

Little-endian, host-independent:

| Offset | Field |
|---|---|
| 0..4 | magic `BSGI` |
| 4..8 | `FORMAT_VERSION` (`uint32`, currently 1) |
| 8..12 | entry count (`uint32`) |
| … | records sorted by canonical `res://` path |
| end-8..end | file checksum (`uint64`) |

Each record:

- length-prefixed UTF-8 path, namespace, qualified head name, base type, icon path
- `uint64` source digest
- `uint32` `BSDeclarationKind`
- `uint8` is_abstract, is_tool
- `uint32` annotation count + sorted length-prefixed annotation names
- `uint8` declares_retroactive_conformances
- `uint64` record checksum over the record body

## Regenerating fixtures

Fixtures are produced by `BaristaScriptDeclarationIndexProbe` under
`template_debug`. See `project/tests/declaration_index_test.gd`.

| File | How it is produced |
|---|---|
| `golden_index.bsi` | Two records (class + declaration-only conformance) flushed under `FORMAT_VERSION` |
| `old_version.bsi` | `golden_index.bsi` with version bytes forced to `FORMAT_VERSION + 1` and checksum recomputed via probe helpers where needed; for negative load tests the suite also mutates bytes in memory |
| `bad_magic.bsi` | golden with magic clobbered |
| `truncated.bsi` | golden truncated |
| `trailing_bytes.bsi` | golden with an extra trailing byte before the file checksum region is invalidated |
| `bad_checksum.bsi` | golden with one payload byte flipped |
