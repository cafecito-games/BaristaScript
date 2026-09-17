# A `tuple_name` file declares one global tuple type. Another script imports the namespace, uses the
# global name as a type annotation, constructs values through it, and tests values against it. The
# runtime test stays structural, so an equally shaped unnamed tuple satisfies it too.
import tuples.geometry

func offset(point: Vec2, delta: float) -> Vec2:
	return Vec2(point.x + delta, point.y + delta)

func test():
	var point: Vec2 = Vec2(1.5, 2.5)
	print(point)
	print(point.x)
	print(point.1)

	var moved := offset(point, 1.0)
	print(moved)

	var boxed: Variant = point
	print(boxed is Vec2)
	print(boxed is (float, float))
	print(boxed is (int, int))

	var shaped: Variant = (3.5, 4.5)
	print(shaped is Vec2)
