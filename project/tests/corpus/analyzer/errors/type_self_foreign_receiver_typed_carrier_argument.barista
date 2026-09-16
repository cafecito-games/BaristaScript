# Receiver identity admits a bare `Self` position and anonymous tuple element positions only. A typed
# carrier is checked against its declared element type invariantly, so an open receiver rejects one
# built from the receiver itself.
class Cell:
	func take_all(items: Array[Self]) -> void:
		pass

	func route(other: Cell) -> void:
		other.take_all([other])


func test() -> void:
	Cell.new().route(Cell.new())
