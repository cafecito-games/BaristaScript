# The retroactive conformance lives in its own file, separate from where RtcTool is used. Loading this
# file (via `preload`) is what brings the `extend RtcTool uses RtcBuildable` conformance, and its
# static witness, into effect for the using code.
extend RtcTool uses RtcBuildable:
	static func build_tag() -> String:
		return prefix + "-tag"
