# A named tuple is constructed by calling its declaration and its fields are readable by name and
# by index. Both erase to the same read-only Array shape at runtime.
tuple Vec2(x: float, y: float)

func test():
	var point := Vec2(1.5, 2.5)
	print(point)
	print(point.x)
	print(point.y)
	print(point.0)
	var erased: (float, float) = point
	print(erased)
