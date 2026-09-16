# A nested `Self` container accepts the already-substituted element type the analyzer itself stamps
# onto a nested literal, and nothing else. A hand-written container of the declaring class is not
# that element: its runtime leaf can be any subclass, so it stays rejected in every nested position.
class Base:
	func grid() -> Array[Array[Self]]:
		var row: Array[Base] = [Base.new()]
		return [row]

	func rows() -> Array[Dictionary[String, Self]]:
		var row: Dictionary[String, Base] = { "item": Base.new() }
		return [row]


class Child extends Base:
	pass


func test() -> void:
	pass
