# Calling a `Type[T]`-parameterized callable with an instance is rejected.
class Factory extends RefCounted:
	pass


func test() -> void:
	var construct: Callable[[Type[Factory]], void] = func(_factory: Type[Factory]) -> void:
		pass
	construct.call(Factory.new())
