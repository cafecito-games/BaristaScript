# A script in the same namespace as a `tuple_name` file reaches the tuple by its bare name without an
# import, for the type annotation and for the constructor call alike.
namespace tuples.samescope

func test():
	var point: SameScopePoint = SameScopePoint(3, 4)
	print(point)
	print(point.x + point.y)
	var boxed: Variant = point
	print(boxed is SameScopePoint)
