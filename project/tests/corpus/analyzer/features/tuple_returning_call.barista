# A function that returns a tuple is still an ordinary call: only a call whose callee is a tuple
# declaration builds a tuple value. Constructor arguments are converted to their declared field types.
tuple Vec2(x: float, y: float)

func make_pair() -> (int, String):
	print("make_pair ran")
	return (1, "one")

func make_point(scale: int) -> Vec2:
	return Vec2(scale, scale)

func test():
	var pair := make_pair()
	print(pair)
	print(pair.0)
	var point := make_point(3)
	print(point)
	print(point.x)
