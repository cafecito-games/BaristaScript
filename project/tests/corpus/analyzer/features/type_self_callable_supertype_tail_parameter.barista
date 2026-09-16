# A callable's rest tail receives values rather than supplying them, so a callback whose tail accepts a
# supertype of the declared element accepts every leaf the parameter can send it. The bound itself and
# anything above it both qualify. Such an argument names no `Self`, so nothing about it is
# caller-relative and the admission holds through a foreign open receiver as well as the calling
# frame's own. A container nested inside the element widens the same way, because ordinary
# compatibility is contravariant through container elements too.
class Super:
	pass


class Cell:
	extends Super

	func fan(callback: Callable[[...Array[Self]], void]) -> void:
		callback.call(self, self)

	func take_bound(...values: Array[Cell]) -> void:
		print("bound ", values.size())

	func take_super(...values: Array[Super]) -> void:
		print("super ", values.size())

	func fan_nested(callback: Callable[[...Array[Array[Self]]], void]) -> void:
		callback.call([self], [self])

	func take_nested(...values: Array[Array[Cell]]) -> void:
		print("nested ", values.size())

	func run(other: Cell) -> void:
		var bound_tail: Callable[[...Array[Cell]], void] = take_bound
		var super_tail: Callable[[...Array[Super]], void] = take_super
		fan(bound_tail)
		other.fan(super_tail)
		var nested_tail: Callable[[...Array[Array[Cell]]], void] = take_nested
		fan_nested(nested_tail)


func test() -> void:
	Cell.new().run(Cell.new())
