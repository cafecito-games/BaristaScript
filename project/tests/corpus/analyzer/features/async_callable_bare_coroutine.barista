# A bare AsyncCallable (no explicit signature) still treats call() as a coroutine whose untyped
# result must be awaited, and it only accepts async callables as values.
async func _async() -> int:
	return 7


func test() -> void:
	var cb: AsyncCallable = _async
	print(cb.is_valid())
	print(await cb.call())
