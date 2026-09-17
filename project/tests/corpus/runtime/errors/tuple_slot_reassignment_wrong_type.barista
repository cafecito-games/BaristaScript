# A later store into an already-initialized tuple local is the same boundary its initializer was, so
# the declared shape cannot be laundered away after the fact.
func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var pair: (int, String) = (1, "one")
	print(pair)
	pair = supply((1, 2))
	print(pair)
