# A static tuple member is a tuple slot too: its store publishes through a temporary whose type is the
# same erased Array carrier, so the declared shape is checked there.
class Holder extends RefCounted:
	static var field: (int, String) = (0, "zero")

	static func store(value: Variant) -> void:
		field = value


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	Holder.store(supply((1, "one")))
	print(Holder.field)
	Holder.store(supply((1, 2, 3)))
	print(Holder.field)
