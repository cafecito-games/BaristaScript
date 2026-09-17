# A synchronous helper starts an async job and hands the in-flight Coroutine[String] handle back
# through its return type; the caller holds the handle and awaits it later. This exercises a
# Coroutine[T] flowing across a function-return boundary at runtime, not just in the analyzer.
async func _work(p_value: int) -> String:
	return "value:" + str(p_value)


func _start(p_value: int) -> Coroutine[String]:
	return _work(p_value)


func test() -> void:
	var first: Coroutine[String] = _start(1)
	var second: Coroutine[String] = _start(2)
	print(await first)
	print(await second)
