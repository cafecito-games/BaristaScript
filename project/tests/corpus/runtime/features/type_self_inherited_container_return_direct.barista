class Base:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


class Child:
	extends Base


func pass_items(child: Child) -> Array[Child]:
	return child.items()


func pass_mapping(child: Child) -> Dictionary[String, Child]:
	return child.mapping()


func test() -> void:
	var child := Child.new()
	var items := pass_items(child)
	print(items[0] is Child, " ", items.get_typed_script() == Child)
	var mapping := pass_mapping(child)
	print(mapping["item"] is Child, " ", mapping.get_typed_value_script() == Child)
