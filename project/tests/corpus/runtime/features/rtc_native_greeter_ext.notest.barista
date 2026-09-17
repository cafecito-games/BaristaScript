# The native retroactive conformance lives in its own file, separate from where the native value is
# used. Loading this file (via `preload`) is what brings the `extend RefCounted uses RtcNativeGreeter`
# conformance into effect for the using code, proving cross-file visibility for native targets.
extend RefCounted uses RtcNativeGreeter:
	func greet() -> int:
		return 7 * 3
