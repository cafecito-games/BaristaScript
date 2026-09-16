# A lambda carries its signature without the explicit-annotation flag, so it is compared through the
# lenient property-form path. The class-handle layer is still part of that comparison.
class Factory extends RefCounted:
	pass


func test() -> void:
	var handle_cb: Callable[[Type[Factory]], void] = func(_factory: Factory) -> void:
		pass
	var instance_cb: Callable[[Factory], void] = func(_factory: Type[Factory]) -> void:
		pass
	print(handle_cb)
	print(instance_cb)
