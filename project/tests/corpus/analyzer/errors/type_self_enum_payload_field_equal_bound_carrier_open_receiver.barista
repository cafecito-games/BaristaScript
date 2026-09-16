# A tagged-union payload's `Self` field answers the same receiver contract as a named tuple's, so a
# carrier typed with the calling frame's `Self` is rejected through another instance's spelling.
class Cell:
	enum Message:
		Empty
		Load(index: int, items: Array[Self])

	func construct_via_base(cell: Cell) -> void:
		var items: Array[Self] = []
		var made := cell.Message.Load(1, items)
		print(made)


func test() -> void:
	pass
