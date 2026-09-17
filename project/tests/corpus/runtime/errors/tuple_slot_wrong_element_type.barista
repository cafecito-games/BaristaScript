# A tuple slot declares an exact shape, so the slot itself is what enforces it. The value arrives
# through an untyped parameter, which the analyzer cannot pin down, and the store rejects it because
# the second element is an int where the declaration says String.
func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var pair: (int, String) = supply((1, 2))
	print(pair)
