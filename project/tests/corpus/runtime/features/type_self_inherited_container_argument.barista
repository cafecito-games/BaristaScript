class Base:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


class Child:
	extends Base


func take_items(items: Array[Child]) -> void:
	print(items[0] is Child, " ", items.get_typed_script() == Child)


func take_mapping(mapping: Dictionary[String, Child]) -> void:
	print(mapping["item"] is Child, " ", mapping.get_typed_value_script() == Child)


func test() -> void:
	var child := Child.new()
	take_items(child.items())
	take_mapping(child.mapping())
