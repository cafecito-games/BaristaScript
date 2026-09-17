class Base:
	func items() -> Array[Self]:
		return [self]

	func mapping() -> Dictionary[String, Self]:
		return { "item": self }


class Child:
	extends Base


class Holder:
	var items: Array[Child]
	var mapping: Dictionary[String, Child]
	var setter_items: Array[Child]:
		set(value):
			setter_items = value


func test() -> void:
	var child := Child.new()
	var holder := Holder.new()
	holder.items = child.items()
	holder.mapping = child.mapping()
	holder.setter_items = child.items()
	print(holder.items[0] is Child)
	print(holder.mapping["item"] is Child)
	print(holder.setter_items[0] is Child)
