trait SuppliesSelf:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


class Holder:
	uses SuppliesSelf


func test() -> void:
	var holder := Holder.new()
	var items: Array[Holder] = holder.items()
	print(items[0] is Holder, " ", items.get_typed_script() == Holder)
	var mapping: Dictionary[String, Holder] = holder.mapping()
	print(mapping["item"] is Holder, " ", mapping.get_typed_value_script() == Holder)
