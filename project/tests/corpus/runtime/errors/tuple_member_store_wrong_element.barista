# Element types are enforced on a member store for the same reason arity is.
class Holder extends RefCounted:
	var field: (int, String) = (0, "zero")

	func store(value: Variant) -> void:
		field = value


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var holder := Holder.new()
	holder.store(supply((1, RefCounted.new())))
	print(holder.field)
