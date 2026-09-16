trait Surface[T]:
	var callback: Callable[[T], T]


class Base[T] uses Surface[T]:
	pass


class Child extends Base[int]:
	pass


func test(child: Child) -> void:
	child.callback.call("wrong")
