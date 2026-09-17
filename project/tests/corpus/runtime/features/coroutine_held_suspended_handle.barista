# A genuinely *suspending* async job is started and its live `Coroutine[String]` handle is held in a
# variable instead of being awaited in place. The job parks on `await go` before producing any
# result, so capturing the call yields the in-flight `FSFunctionState`. Holding that handle is
# intentional, not a missing-await bug, so storing it no longer trips the debug "async function
# without await" guard. The job is resumed synchronously by emitting the signal it parked on; the
# result is observed through a member side effect because the test harness calls `test()` once with
# no main loop, so an `await` on the held handle could never be resumed.
signal go

var _result: String = ""


async func _job() -> String:
	await go
	_result = "job-done"
	return _result


func test() -> void:
	var handle: Coroutine[String] = _job()
	# The body parked at `await go`, so nothing ran past the await yet and the handle is a live object.
	print("held a handle: ", handle != null)
	print("before resume: '%s'" % _result)
	go.emit()
	print("after resume: '%s'" % _result)
