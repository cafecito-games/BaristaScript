# A typed container validates its elements against exactly one runtime type, which a set of
# alternatives cannot supply, so a union is not a container element type -- naming `Self` inside one
# does not change that. This is what keeps the `Self` element rules free of a union case: element,
# key, value, and nested element positions are all refused at the annotation.
class Receiver:
	func take(_items: Array[int | (int, Self)]) -> void:
		pass

	func take_value(_items: Dictionary[String, int | (int, Self)]) -> void:
		pass

	func take_key(_items: Dictionary[int | (int, Self), String]) -> void:
		pass

	func take_nested(_items: Array[Array[int | (int, Self)]]) -> void:
		pass


func test() -> void:
	pass
