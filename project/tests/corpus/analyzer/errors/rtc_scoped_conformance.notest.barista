# Conforms RtcScopedWidget to RtcScopedTrait. A file that wants this conformance has to load *this*
# file. `retroactive_conformance_unloaded_declaring_file` deliberately does not, while
# `runtime/features/retroactive_conformance_loaded_declaring_file` does — so during a run this
# conformance is registered, and the file that skipped it still must not see it.
extend RtcScopedWidget uses RtcScopedTrait:
	static func mark() -> int:
		return 3
