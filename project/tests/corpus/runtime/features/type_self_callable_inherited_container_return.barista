class Base:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	var items_cb := child.items
	var items: Array[Child] = items_cb.call()
	print(items[0] is Child, " ", items.get_typed_script() == Child)

	var mapping_cb := child.mapping
	var mapping: Dictionary[String, Child] = mapping_cb.call()
	print(mapping["item"] is Child, " ", mapping.get_typed_value_script() == Child)
