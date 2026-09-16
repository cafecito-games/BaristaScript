# A type-argument list belongs to the last name of a qualified type. Writing it on an earlier name is
# a parse error naming the suffix position rather than a downstream lookup failure.
class Outer:
	class Box[T]:
		var value: T


func test(box: Outer[int].Box) -> void:
	print(box)
