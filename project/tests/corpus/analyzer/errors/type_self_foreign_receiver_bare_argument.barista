# The caller's `Self` is the calling frame's receiver and the callee's is whatever `other` turns out to
# be, so passing the current `self` into a foreign open receiver's `Self` position is rejected.
class Cell:
	func take(other: Self) -> void:
		pass

	func route(other: Cell) -> void:
		other.take(self)


func test() -> void:
	Cell.new().route(Cell.new())
