# A reference to an async/coroutine method infers an AsyncCallable, so it is assignable to an
# explicit AsyncCallable target whose slots match. If the async marker were not inferred, the
# reference would be a plain Callable and this assignment would be rejected.
async func _fetch() -> int:
	return 42


func test() -> void:
	var typed: AsyncCallable[[], int] = _fetch
	print(typed.is_valid())
