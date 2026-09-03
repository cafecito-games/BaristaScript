# Parse-cache fixtures

`script_a.barista` and `script_b.barista` are the real sources the cache keys on;
`cache_test.gd` digests their bytes and stores a parse payload derived from them.

Every `*.bin` in this directory is produced by this build's own cache writer
(`BSParseCache::flush`), not typed by hand. Regenerate them with:

```
godot --headless --path project --script res://tests/cache_test.gd -- --regenerate
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
