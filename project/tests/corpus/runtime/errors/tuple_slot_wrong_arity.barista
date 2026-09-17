# Arity is part of a tuple's shape: a three-element Array is not a `(int, String)` even though both
# leading elements match.
func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var pair: (int, String) = supply([1, "two", 3])
	print(pair)
