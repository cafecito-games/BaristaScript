# A separately named local that merely stores the receiver is not proof of receiver identity, so an
# open receiver rejects it in a `Self` position exactly as it rejects any other value of its class.
class Cell:
	func take(other: Self) -> void:
		pass

	func route(other: Cell) -> void:
		var alias := other
		other.take(alias)


func test() -> void:
	Cell.new().route(Cell.new())
