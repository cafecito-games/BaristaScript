# Offline parser producer inputs

These `.fs` and `.out` files are byte-for-byte copies from the source paths and
immutable Foundry revision in `provenance.json`; SHA-256 hashes verify every
fixture. They cover each existing parser triage transformation plus ordinary
sources and a skipped helper. They are data only: no upstream code is executed.

The default offline tests call the real parser producer on this small set and
exercise its byte comparison. This is not full corpus provenance verification.
Pass `--foundry /path/to/clean/pinned/Foundry` for the full 340-case check and
mutations against real upstream inputs; CI does so after its isolated checkout.
