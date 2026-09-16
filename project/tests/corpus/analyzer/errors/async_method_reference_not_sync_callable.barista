# A reference to an async method infers an AsyncCallable, which is not assignable to a plain
# (synchronous) Callable target with otherwise-matching slots.
async func _fetch() -> int:
	return 42


func test() -> void:
	var typed: Callable[[], int] = _fetch
	print(typed)
