# The retroactive conformance lives in its own file, separate from where RtcPanel is used. Loading
# this file (via `preload`) is what brings the `extend RtcPanel uses RtcMeasurable` conformance into
# effect for the using code. The witness reads the target's own `level` member.
extend RtcPanel uses RtcMeasurable:
	func size() -> int:
		return level * 3
