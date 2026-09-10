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

## Fixture provenance and validation

The five fixtures were produced by `BaristaScriptDeclarationIndexProbe` under
`template_debug` via `project/tests/declaration_index_test.gd` at immutable commit
`1fe5964cd78150651a9d9a4890586ee6a5245aa2`. `tests/native/declaration_index_test.cpp`
preserves the same typed records and mutations, compares disposable candidate bytes to
these read-only fixtures, and exercises the production loader on both copies. Validate with:

```
scons api_version=4.7 target=template_debug barista_tests=yes
python3 tests/run_native_suites.py --godot /path/to/stock-godot --suite declaration_index
```

| File | How it is produced |
|---|---|
| `golden_index.bsi` | Two records (class + declaration-only conformance) flushed under `FORMAT_VERSION` |
| `old_version.bsi` | Golden with version byte at offset 4 set to `FORMAT_VERSION + 1` and its last checksum byte incremented; version rejection takes precedence |
| `bad_magic.bsi` | golden with magic clobbered |
| `truncated.bsi` | golden truncated |
| `bad_checksum.bsi` | golden with one payload byte flipped |
