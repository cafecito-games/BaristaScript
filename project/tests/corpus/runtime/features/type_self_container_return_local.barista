class Base:
	func items() -> Array[Self]:
		var result_items: Array[Self] = [self]
		return result_items

	func mapping() -> Dictionary[String, Self]:
		var result_mapping: Dictionary[String, Self] = { "item": self }
		return result_mapping


func test() -> void:
	var base := Base.new()
	var items: Array[Base] = base.items()
	print(items[0] is Base, " ", items.get_typed_script() == Base)
	var mapping: Dictionary[String, Base] = base.mapping()
	print(mapping["item"] is Base, " ", mapping.get_typed_value_script() == Base)
