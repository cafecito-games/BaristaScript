class Base:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	var items: Array[Child] = child.items()
	print(items[0] is Child, " ", items.get_typed_script() == Child)
	var mapping: Dictionary[String, Child] = child.mapping()
	print(mapping["item"] is Child, " ", mapping.get_typed_value_script() == Child)
