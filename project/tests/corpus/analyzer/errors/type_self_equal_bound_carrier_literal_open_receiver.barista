# A container literal written from `self` is the analyzer's own substitution of the calling frame's
# `Self`, so it is exactly as caller-relative as a `Self`-typed local: an open receiver's `Self`
# positions still require the call to run through the calling frame's own receiver.
class Cell:
	func take(_items: Array[Self]) -> void:
		pass

	func take_mapping(_mapping: Dictionary[String, Self]) -> void:
		pass

	func call_via_base(cell: Cell) -> void:
		cell.take([self])
		cell.take_mapping({ "item": self })


func test() -> void:
	Cell.new().call_via_base(Cell.new())
