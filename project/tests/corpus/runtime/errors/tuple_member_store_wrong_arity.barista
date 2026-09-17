# A tuple member's slot erases to a bare Array like every other tuple slot, so an in-body store from a
# source the analyzer cannot judge is checked against the declared shape.
class Holder extends RefCounted:
	var field: (int, String) = (0, "zero")

	func store(value: Variant) -> void:
		field = value


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var holder := Holder.new()
	holder.store(supply((1, "one")))
	print(holder.field)
	# The store fails inside `store()`, which aborts that frame; the member keeps the value it had.
	holder.store(supply((1, 2, 3)))
	print(holder.field)
