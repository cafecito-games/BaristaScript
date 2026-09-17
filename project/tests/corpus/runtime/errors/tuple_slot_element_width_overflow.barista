# The tuple slot store shares the structural test with the `is` operator, so an element whose
# magnitude the declared width cannot hold is rejected at the store with the standard
# variable-assignment diagnostic rather than being written into an `int` slot it does not fit.
func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var wide: Variant = supply([9223372036854775807, "x"])
	print(wide is (int, String))
	var slot: (int, String) = wide
	print(slot)
