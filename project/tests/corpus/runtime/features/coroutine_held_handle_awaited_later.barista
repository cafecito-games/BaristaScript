# Epic capstone: "start now, hold the handle, await later" end to end. A genuinely *suspending* async
# job is started and its live `Coroutine[String]` handle is captured into a named variable (`handle`)
# instead of being awaited in place. The job parks on `await go` before producing any result, so the
# capture yields the in-flight `FSFunctionState`; holding it no longer trips the debug "async
# function without await" guard. Unlike the side-effect fixtures, the result here is consumed by an
# actual `await handle` rather than observed through a member write.
#
# `test()` cannot await the handle itself: the harness calls `test()` once with no main loop, so a
# park inside `test()` could never resume. The held handle is therefore handed to a fire-and-forget
# runner started *before* the resume signal fires, so its `await handle` connects while the job is
# still suspended. A single `go.emit()` then resumes the job synchronously, whose completion resumes
# the runner and yields the awaited String. This gives a deterministic order with no frame/timer
# pumping while exercising the held-by-name (PR #621) plus awaited-later (PR #616) flow together.
signal go

var _drained: String = ""


async func _job(p_value: int) -> String:
	await go
	return "value:" + str(p_value)


async func _drain(p_handle: Coroutine[String]) -> void:
	_drained = await p_handle


func test() -> void:
	var handle: Coroutine[String] = _job(1)
	# The body parked at `await go`, so nothing ran past the await yet and the handle is a live object.
	print("held a handle: ", handle != null)

	_drain(handle)
	# The runner is parked inside `await handle`; the job is still parked on `go`. Nothing resumed yet.
	print("before resume: '%s'" % _drained)

	go.emit()
	print("after resume: '%s'" % _drained)
