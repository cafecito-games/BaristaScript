# Supplies `frw_gadget()` for `FrwWidget` from a separate file, and spells the witness's helper
# signature with an alias declared here rather than in the target's file.
type Meters = float

extend FrwWidget uses FrwGadgetlike:
	func frw_gadget() -> String:
		return "gadget:" + str(measure(1.5))

	func measure(distance: Meters) -> Meters:
		return distance * 2.0
