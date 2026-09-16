# Invoking a bare AsyncCallable yields a Coroutine[Variant]. Without "await" you hold the coroutine
# handle, not its result, so binding it to a concretely-typed variable is a type error.
async func _async() -> int:
	return 1


func test() -> void:
	var cb: AsyncCallable = _async
	var value: int = cb.call()
	print(value)
