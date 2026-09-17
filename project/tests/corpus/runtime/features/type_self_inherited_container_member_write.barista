class Base:
	var items: Array[Self]
	var mapping: Dictionary[String, Self]


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	child.items = [child]
	child.mapping = { "item": child }
	print(child.items[0] is Child)
	print(child.mapping["item"] is Child)
