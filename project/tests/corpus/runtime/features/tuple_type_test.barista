# Tuple type tests check the shape a tuple erases to: an Array of the declared arity whose elements
# pass their own type tests. Arity and element types both matter, and `is not` is the plain negation.
tuple Vec2(x: float, y: float)

func test():
	var pair: Variant = (1, "one")
	print(pair is (int, String))
	print(pair is (String, int))
	print(pair is (int, String, int))
	print(pair is (int, int))
	print(pair is not (String, int))

	var point: Variant = Vec2(1.5, 2.5)
	print(point is Vec2)
	print(point is (float, float))
	print(point is (int, int))

	var nested: Variant = ((1, 2), "tail")
	print(nested is ((int, int), String))
	print(nested is ((int, String), String))

	var scalar: Variant = 7
	print(scalar is (int, String))

	var typed: Variant = ([1, 2], 3)
	print(typed is (Array, int))
