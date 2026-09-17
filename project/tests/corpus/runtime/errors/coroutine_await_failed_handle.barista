# Regression for #1944 (error path): a suspended coroutine that fails with a runtime error after
# resume latches the default value of its declared return type. A later `await` of its handle
# resolves immediately with that default (empty String here) instead of parking forever. The runtime
# error itself is reported at failure time exactly as before; no new error object is introduced.
signal go


async func _failing_job() -> String:
	await go
	var values: Array[int] = [1, 2, 3]
	return str(values[10])


async func _drain(p_handle: Coroutine[String]) -> void:
	print("drained after error: '", await p_handle, "'")


func test() -> void:
	_drain(_failing_job())
	go.emit()
