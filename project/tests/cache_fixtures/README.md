# Parse-cache fixtures

`script_a.barista` and `script_b.barista` are the real sources the cache keys on;
`tests/native/cache_test.cpp` digests their bytes and stores a parse payload derived from them.

The binary fixtures were produced from `BSParseCache::flush` output by the legacy generator
in the retired GDScript cache suite at immutable commit
`1fe5964cd78150651a9d9a4890586ee6a5245aa2`. The native migration preserves those bytes and
compares newly written disposable stores to the golden. Validate without rewriting fixtures:

```
scons api_version=4.7 target=template_debug barista_tests=yes
python3 tests/run_native_suites.py --godot /path/to/stock-godot --suite cache
```

| File | How it is produced |
|---|---|
| `golden_store.bin` | Both fixtures flushed under the current `CACHE_FORMAT_VERSION` |
| `version_mismatch_store.bin` | The same flush under `CACHE_FORMAT_VERSION + 1` |
| `truncated_store.bin` | `golden_store.bin` with its last six bytes removed |
| `corrupt_store.bin` | `golden_store.bin` with one byte inside the first record flipped |
| `bad_magic_store.bin` | `golden_store.bin` with its magic clobbered |
| `duplicate_key_store.bin` | The first record of a one-entry flush repeated, with the header's entry count set to two |
| `duplicate_key_across_versions_store.bin` | The same, but the second record is the `CACHE_FORMAT_VERSION + 1` flush of that key |

Regenerating `golden_store.bin` after a record-layout change without bumping
`CACHE_FORMAT_VERSION` is exactly the mistake the version constant exists to
catch, so bump it in the same change.
