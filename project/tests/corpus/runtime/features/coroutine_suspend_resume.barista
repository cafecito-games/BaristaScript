# A genuinely suspending Coroutine[String]: the async job hits an internal `await` mid-body, so the
# call does not run to completion. Awaiting the call (`await _job(1)`) connects to the still-live
# FSFunctionState's "completed" signal and parks the caller until the job is resumed later,
# at which point the awaited expression yields the job's String result. This exercises the live
# function-state handoff that the eager-completion fixtures (coroutine_fan_out, return_type_handoff)
# deliberately avoid.
#
# The job is awaited from a fire-and-forget runner started before the resume signal fires, so the
# `await` connects while the job is still suspended; awaiting an already-finished job would wait
# forever on its one-shot "completed" signal. Emitting `go` then resumes the job synchronously, whose
# completion resumes the runner, giving a deterministic order without relying on frame/timer pumping.
#
# (A genuinely suspended handle cannot be stored in a bare `Coroutine[String]` variable or array in a
# debug build: capturing the result of an async call without awaiting it raises a runtime error. The
# handle is therefore awaited in place rather than held by name.)
signal go


async func _job(p_value: int) -> String:
	await go
	return "value:" + str(p_value)


async func _runner() -> void:
	var result: String = await _job(1)
	print("drained: ", result)


func test() -> void:
	_runner()
	# The runner is parked inside `await _job(1)` and the job is parked on `go`; nothing has resumed.
	print("before resume")

	go.emit()
	print("after resume")
