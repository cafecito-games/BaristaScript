# Tuples compare by value: equality is the deep Array comparison, so a named tuple and an unnamed
# tuple with equal elements are equal (names are erased at runtime). Because the runtime value is a
# read-only Array it hashes by content and is usable as a Dictionary key, and it survives a round trip
# through a `Variant`-typed slot unchanged.
tuple Vec2(x: float, y: float)

func round_trip(value: Variant) -> Variant:
	return value

func test():
	print((1, 2) == (1, 2))
	print((1, 2) == (2, 1))
	print((1, 2) != (1, 2, 3))
	print(Vec2(1.0, 2.0) == (1.0, 2.0))

	var nested_left := ((1, 2), (3, 4))
	var nested_right := ((1, 2), (3, 4))
	print(nested_left == nested_right)

	var scores := {}
	scores[(1, 2)] = "origin"
	scores[Vec2(3.0, 4.0)] = "corner"
	print(scores[(1, 2)])
	print(scores[(3.0, 4.0)])
	print(scores.size())

	var escaped: Variant = round_trip((5, 6))
	print(escaped)
	print(escaped == (5, 6))
	var restored: (int, int) = escaped
	print(restored.0)
