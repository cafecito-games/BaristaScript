# AsyncCallable and plain Callable are not interchangeable even when their parameter/return slots
# match: the async marker is part of signature equality. Assigning across the two is rejected in
# both directions, and async-aware stringification names each side distinctly in the diagnostic.
func _sync(value: int) -> String:
	return str(value)


async func _async(value: int) -> String:
	return str(value)


func test() -> void:
	var sync_cb: Callable[[int], String] = _sync
	var async_cb: AsyncCallable[[int], String] = _async
	var bad_async: AsyncCallable[[int], String] = sync_cb
	var bad_sync: Callable[[int], String] = async_cb
	print(bad_async)
	print(bad_sync)
