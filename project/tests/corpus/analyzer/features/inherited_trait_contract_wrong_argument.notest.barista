trait Contract[T]:
	var value: T
	var values: Array[T]
	signal changed(item: T)

	func accept(item: T) -> T:
		return item

	func project[U](owned: T, item: U) -> U:
		return item


class Base[T] uses Contract[T]:
	pass


class Child extends Base[int]:
	pass


func test(child: Child) -> void:
	child.accept("wrong")
