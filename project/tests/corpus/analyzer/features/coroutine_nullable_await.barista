# The nullable variant Coroutine[T]? is allowed (the handle is a RefCounted object that can be null).
# Awaiting it can observe a null handle, so the awaited result is the nullable T and assigns to a
# nullable variable.
async func _work() -> String:
	return "ok"


func test() -> void:
	var job: Coroutine[String]? = _work()
	var result: String? = await job
	print(result)
