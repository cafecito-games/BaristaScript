# Coroutine[T] is nameable in annotations and assignable from a matching unawaited async call.
# Awaiting it yields T. The Coroutine[void] variant is allowed for void-returning async work.
async func _go() -> int:
	return 5


async func _nothing() -> void:
	pass


func test() -> void:
	var job: Coroutine[int] = _go()
	var value: int = await job

	var task: Coroutine[void] = _nothing()
	await task

	print(value)
