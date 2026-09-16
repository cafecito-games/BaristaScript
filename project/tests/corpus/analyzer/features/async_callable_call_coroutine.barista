# Invoking an AsyncCallable yields a coroutine whose result is the callable's declared return type,
# so the call must be awaited and the awaited value keeps that type. bind() and unbind() preserve
# the async marker, so the transformed callables stay coroutines too (no redundant-await warning).
async func _work(value: int) -> String:
	return str(value)


func test() -> void:
	var handler: AsyncCallable[[int], String] = _work
	var result: String = await handler.call(7)
	print(result)

	var bound := handler.bind(9)
	print(await bound.call())

	var unbound := handler.unbind(1)
	print(await unbound.call(3, "ignored"))
