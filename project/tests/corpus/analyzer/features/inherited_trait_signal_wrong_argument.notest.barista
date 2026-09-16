trait Surface[T]:
	signal changed(item: T)


class Base[T] uses Surface[T]:
	pass


class Child extends Base[int]:
	pass


func test(child: Child) -> void:
	child.changed.emit("wrong")
