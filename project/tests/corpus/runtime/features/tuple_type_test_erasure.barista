# A tuple type test is structural by design: nominal identity is erased at runtime, exactly like the
# int backing of an ordinary enum. A plain Array of the right shape therefore satisfies a named tuple
# test, and a named tuple satisfies the test of any other tuple with the same element types.
tuple Vec2(x: float, y: float)
tuple Size2(width: float, height: float)

func test():
	var plain: Variant = [1.5, 2.5]
	print(plain is Vec2)

	var point: Variant = Vec2(1.5, 2.5)
	print(point is Size2)

	# The erasure is shape-only: a mismatched arity or element type still fails.
	var short_array: Variant = [1.5]
	print(short_array is Vec2)
	var wrong_elements: Variant = ["a", "b"]
	print(wrong_elements is Vec2)
