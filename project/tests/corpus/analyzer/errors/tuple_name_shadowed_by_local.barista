# A local binding of the same name shadows the tuple declaration, so the call never constructs a
# tuple. Calling the shadowed name is rejected like any other non-function member.
tuple Vec2(x: float, y: float)

func test():
	var Vec2 := func(a: float, b: float) -> float: return a + b
	print(Vec2(1.0, 2.0))
