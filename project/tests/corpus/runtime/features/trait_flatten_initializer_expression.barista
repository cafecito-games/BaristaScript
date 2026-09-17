extends RefCounted
uses Stamped

trait Stamped:
	var items: Array[int] = []
	func add(x: int) -> void:
		items.append(x)

func test() -> void:
	add(1)
	add(2)
	print(items.size())
	print(items[0])
