# A `final` class's `Self` names exactly one class, so there is no leaf two frames could disagree
# about: a carrier typed with `Self` stays admitted through an open receiver, and the callee's typed
# container accepts it at run time.
final class Cell:
	func take(items: Array[Self]) -> void:
		print("took ", items.size())

	func route(cell: Cell) -> void:
		var items: Array[Self] = [self]
		cell.take(items)


func test() -> void:
	var cell := Cell.new()
	cell.route(cell)
