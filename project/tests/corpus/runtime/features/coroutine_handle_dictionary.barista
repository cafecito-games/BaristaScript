# Genuinely suspending async jobs whose live handles are held as the values of a typed
# `Dictionary[String, Coroutine[String]]`. Each job parks on `await go` before recording anything,
# so storing the call result in the dictionary captures the in-flight `FSFunctionState`
# instead of a finished value. Holding the handles this way is intentional, not a missing-await bug,
# so it no longer trips the debug "async function without await" guard. The jobs are resumed in one
# shot and results observed through a member side effect, because the test harness calls `test()`
# once with no main loop to resume an awaited handle.
signal go

var _results: Array[String] = []


async func _job(p_tag: String) -> String:
	await go
	var value: String = "done:" + p_tag
	_results.append(value)
	return value


func test() -> void:
	var handles: Dictionary[String, Coroutine[String]] = {
		"first": _job("first"),
		"second": _job("second"),
	}
	print("held handles: ", handles.size())
	print("before resume: ", _results)
	go.emit()
	print("after resume: ", _results)
