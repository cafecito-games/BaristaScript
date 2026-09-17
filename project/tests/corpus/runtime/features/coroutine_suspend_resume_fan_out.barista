# Structured-concurrency fan-out over genuinely suspending jobs. Every job hits an internal `await`
# on a shared signal mid-body, so all three are started and parked concurrently before any of them
# resumes. Each is awaited from its own fire-and-forget runner (`await _job(value)`), which connects
# to the job's still-live FSFunctionState while it is suspended. A single `go.emit()` then
# releases every job in start order, and each completion resumes its runner to collect the String
# result. This exercises the live function-state handoff for a fan-out, which the eager-completion
# coroutine_fan_out fixture deliberately avoids.
#
# (A genuinely suspended handle cannot be stored in an `Array[Coroutine[String]]` in a debug build:
# capturing the result of an async call without awaiting it raises a runtime error. Each handle is
# therefore awaited in place by a runner rather than collected into a typed array, and results are
# gathered in the shared `_results` array in deterministic start order.)
signal go

var _results: Array[String] = []


async func _job(p_value: int) -> String:
	await go
	return "value:" + str(p_value)


async func _runner(p_value: int) -> void:
	_results.append(await _job(p_value))


func test() -> void:
	for value: int in [1, 2, 3]:
		_runner(value)
	# Every job is parked on the shared signal; no runner has collected a result yet.
	print("before resume: ", _results)

	# A single emit releases all suspended jobs in start order; each completion resumes its runner.
	go.emit()
	print("after resume: ", _results)
