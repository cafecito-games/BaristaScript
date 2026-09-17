# Regression for #1944: `await` on a `Coroutine[T]` handle whose coroutine already finished parked the
# awaiter forever on a one-shot `completed` signal that had fired exactly once. After the fix, the
# second (and every later) `await` of a resolved handle resolves immediately with the latched result.
#
# The job genuinely suspends on `await go`, so calling it yields an in-flight `Coroutine[String]`. A
# fire-and-forget drain runner is started before the resume signal fires, so its first `await` connects
# while the job is still suspended. A single `go.emit()` resumes the job synchronously; the job
# completes and latches its result, which resumes the drain's first await, then the drain's second
# await finds the handle already resolved and yields the same latched value without parking.
signal go


async func _job() -> String:
	await go
	return "done"


async func _drain(p_handle: Coroutine[String]) -> void:
	print("first await: ", await p_handle)
	print("second await: ", await p_handle)
	print("drain finished")


func test() -> void:
	_drain(_job())
	# The drain is parked inside its first `await p_handle`; the job is parked on `go`.
	go.emit()
