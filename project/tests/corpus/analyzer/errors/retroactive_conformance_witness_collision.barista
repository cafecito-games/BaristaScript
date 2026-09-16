# The same target conforms to two different traits, each supplying a witness named `describe()`.
# Runtime witness dispatch keys on (target, method name) only, so this must be rejected.
extend RtcWitnessCollisionGadget uses RtcWitnessCollisionTraitA:
	func describe() -> String:
		return "a"


extend RtcWitnessCollisionGadget uses RtcWitnessCollisionTraitB:
	func describe() -> String:
		return "b"
