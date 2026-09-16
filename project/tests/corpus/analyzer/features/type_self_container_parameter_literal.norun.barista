final class Base:
	final func take_items(_items: Array[Self]) -> void:
		pass

	final func take_mapping(_mapping: Dictionary[String, Self]) -> void:
		pass

	func check() -> void:
		take_items([self])
		take_mapping({ "item": self })


func test() -> void:
	Base.new().check()
