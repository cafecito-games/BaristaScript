# A bare AsyncCallable.call() statement at root position warns about the missing "await", matching
# the diagnostic for a discarded coroutine call.
async func _work(value: int) -> String:
	return str(value)


func test() -> void:
	var handler: AsyncCallable[[int], String] = _work
	handler.call(7)
