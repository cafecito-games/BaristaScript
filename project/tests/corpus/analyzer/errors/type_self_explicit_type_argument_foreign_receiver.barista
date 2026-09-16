# The bracket spelling accepts what the bracket-free one accepts and rejects what it rejects: the
# calling frame's `self` is still a foreign value in an open receiver's `Self` position.
class Cell:
	func pair[T](first: T, second: Self) -> void:
		pass

	func route(other: Cell) -> void:
		other.pair[int](1, self)


func test() -> void:
	Cell.new().route(Cell.new())
