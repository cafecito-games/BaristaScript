# An anonymous tuple carries the receiver-identity requirement into its `Self` element positions, so a
# tuple literal holding the calling frame's `self` is rejected for a foreign open receiver.
class Cell:
	func take_pair(pair: (int, Self)) -> void:
		pass

	func route(other: Cell) -> void:
		other.take_pair((1, self))


func test() -> void:
	Cell.new().route(Cell.new())
