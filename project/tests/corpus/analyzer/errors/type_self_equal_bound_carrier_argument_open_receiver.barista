# A carrier typed with the calling frame's `Self` reifies to that frame's receiver, while an open
# receiver's `Self` is whatever that receiver turns out to be. The two agree only when the call runs
# through the calling frame's own receiver, so passing one to another instance is rejected here instead
# of failing the callee's typed-container check at run time.
class Cell:
	func take(_items: Array[Self]) -> void:
		pass

	func take_mapping(_mapping: Dictionary[String, Self]) -> void:
		pass

	func call_via_base(cell: Cell) -> void:
		var items: Array[Self] = []
		var mapping: Dictionary[String, Self] = {}
		cell.take(items)
		cell.take_mapping(mapping)


func test() -> void:
	Cell.new().call_via_base(Cell.new())
