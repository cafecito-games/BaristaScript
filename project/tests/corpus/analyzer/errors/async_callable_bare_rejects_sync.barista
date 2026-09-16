# A bare AsyncCallable target requires an async value: assigning a synchronous callable is rejected
# even though neither side carries an explicit method signature.
func _sync() -> int:
	return 1


func test() -> void:
	var cb: AsyncCallable = _sync
	print(cb)
