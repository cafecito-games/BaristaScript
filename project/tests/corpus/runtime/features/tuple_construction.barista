# Tuple values are built by a dedicated construction opcode that erases them to a read-only Array.
# Unnamed literals and named-tuple construction produce the same runtime shape, elements are readable
# by index and by field name, and nesting works because an element is just another tuple value.
tuple Vec2(x: float, y: float)

func build_pair(first: int, second: int) -> (int, int):
	return (first, second)

func test():
	var pair := (1, 2)
	print(pair)
	print(pair.0)
	print(pair.1)

	var point := Vec2(1.5, 2.5)
	print(point)
	print(point.x)
	print(point.y)
	print(point.1)

	var computed := build_pair(3, 4)
	print(computed)
	print(computed.0 + computed.1)

	var nested := (pair, point)
	print(nested)
	print(nested.0.1)
	print(nested.1.x)

	var escaped: Variant = pair
	print(escaped.is_read_only())
