class Base:
	func take_items(_items: Array[Self]) -> void:
		pass

	func take_type(_factory: Type[Self]) -> void:
		pass


class Child:
	extends Base


func test() -> void:
	var base: Base = Child.new()
	base.take_items(base)
	base.take_type(base)
