# AsyncCallable parses as an async-marked variant of the typed Callable. This covers the
# parser/AST support: an explicit signature, a nullable variant, an empty parameter list, a
# nested signature, and the bare form without a signature. A null nullable target is compared
# against null rather than called, since calling a method on null is a runtime error.
# AsyncCallable targets require async callables, and invoking one yields a coroutine, so
# handler.call(...) must be awaited.
async func _double(value: int) -> String:
	return str(value * 2)


async func _noop() -> void:
	pass


async func _take_cb(_cb: Callable[[int], void]) -> void:
	pass


func test() -> void:
	var handler: AsyncCallable[[int], String] = _double
	var nullable_handler: AsyncCallable[[int], String]? = null
	var no_args: AsyncCallable[[], void] = _noop
	var nested: AsyncCallable[[Callable[[int], void]], void] = _take_cb
	var bare: AsyncCallable = _double
	print(await handler.call(2))
	print(nullable_handler == null)
	print(no_args.is_valid())
	print(nested.is_valid())
	print(bare.is_valid())
