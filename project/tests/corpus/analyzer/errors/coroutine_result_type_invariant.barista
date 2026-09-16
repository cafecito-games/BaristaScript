# Coroutine[T] is invariant in its result type: a Coroutine[int] is not a Coroutine[String], so the
# element types must match exactly, mirroring typed-array element invariance.
async func _number() -> int:
	return 1


func test() -> void:
	var job: Coroutine[String] = _number()
	print(job)
