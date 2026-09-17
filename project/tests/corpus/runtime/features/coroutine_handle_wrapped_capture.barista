# The held handle does not have to be a direct call: a coroutine call wrapped in a ternary or a cast
# still flows into a hard `Coroutine[String]` slot, so the inner calls are compiled as async and the
# in-flight handle is preserved instead of tripping the missing-await guard. Each job parks on
# `await go`, is resumed in one shot, and its result is observed through a member side effect because
# the test harness calls `test()` once with no main loop to resume an awaited handle.
signal go

var _results: Array[String] = []


async func _job(p_tag: String) -> String:
	await go
	var value: String = "done:" + p_tag
	_results.append(value)
	return value


func test() -> void:
	var prefer_first: bool = true
	var ternary_handle: Coroutine[String] = _job("ternary") if prefer_first else _job("unused")
	var cast_handle: Coroutine[String] = _job("cast") as Coroutine[String]
	print("held handles: ", ternary_handle != null and cast_handle != null)
	print("before resume: ", _results)
	go.emit()
	print("after resume: ", _results)
