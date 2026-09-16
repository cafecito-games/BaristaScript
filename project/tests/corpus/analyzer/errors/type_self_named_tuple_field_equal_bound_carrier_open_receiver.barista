# A named tuple's `Self` field is the constructing receiver's, and a carrier's element type is checked
# invariantly, so a carrier typed with the calling frame's `Self` is admissible only when the
# construction runs through that same receiver.
class Cell:
	tuple Crate(index: int, items: Array[Self])

	func construct_via_base(cell: Cell) -> void:
		var items: Array[Self] = []
		var made := cell.Crate(1, items)
		print(made.index)


func test() -> void:
	pass
