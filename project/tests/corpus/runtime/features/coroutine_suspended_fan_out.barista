# Structured-concurrency fan-out where every job genuinely suspends. Each job parks on `await go`
# before recording any result, so calling it yields an in-flight `FSFunctionState`. All the
# live handles are held together in an `Array[Coroutine[String]]` (start now), which no longer trips
# the debug "async function without await" guard, then the jobs are resumed in one shot (await/finish
# later). Results are observed through a member side effect because the test harness calls `test()`
# once with no main loop to resume an awaited handle; the one-shot `await go` connections fire in the
# order the jobs were started, so the collected order matches the call order.
signal go

var _results: Array[String] = []


async func _job(p_tag: String) -> String:
	await go
	var value: String = "done:" + p_tag
	_results.append(value)
	return value


func test() -> void:
	var tags: Array[String] = ["a", "b", "c"]
	var jobs: Array[Coroutine[String]] = []
	for tag: String in tags:
		jobs.append(_job(tag))

	# Every job parked before its body recorded anything, so no result exists until the resume.
	print("held handles: ", jobs.size())
	print("before resume: ", _results)
	go.emit()
	print("after resume: ", _results)
