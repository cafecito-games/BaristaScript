# Invoking an AsyncCallable produces a Coroutine[String]. Without "await" the value is the in-flight
# coroutine handle, not its result, so binding it to a String-typed variable is a type error.
async func _work(value: int) -> String:
	return str(value)


func test() -> void:
	var handler: AsyncCallable[[int], String] = _work
	var result: String = handler.call(7)
	print(result)
