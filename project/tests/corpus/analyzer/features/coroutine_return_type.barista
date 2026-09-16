# Coroutine[T] threads through return types: a synchronous helper can start an async job and return
# the in-flight handle for the caller to await later. The unawaited call in return position is not
# a discarded statement, so it raises no missing-await diagnostic.
async func _work(p_value: int) -> String:
	return str(p_value)


func start(p_value: int) -> Coroutine[String]:
	return _work(p_value)


func test() -> void:
	var job: Coroutine[String] = start(7)
	print(await job)
