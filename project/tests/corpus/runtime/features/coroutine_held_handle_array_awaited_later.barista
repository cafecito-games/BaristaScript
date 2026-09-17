# Epic capstone (fan-out): hold several genuinely suspending handles by name in an
# `Array[Coroutine[String]]`, then await each one later. Every job parks on the shared `await go`
# before producing a result, so each call yields an in-flight `FSFunctionState`; collecting the
# live handles into the typed array no longer trips the debug "async function without await" guard.
# Unlike the side-effect fan-out fixtures, every result is consumed by an actual `await jobs[i]`.
#
# `test()` cannot await the handles itself (the harness calls it once with no main loop), so each held
# handle is handed to a fire-and-forget runner started *before* the resume signal fires; every runner
# parks inside `await p_handle` while its job is still suspended. A single `go.emit()` then releases
# the jobs in start order, and each completion resumes the runner that holds its handle, appending the
# awaited String. The one-shot connections fire in registration order, so the drained order matches
# the order the handles were collected. This exercises held-by-name (PR #621) plus awaited-later
# (PR #616) across a fan-out, deterministically and with no frame/timer pumping.
signal go

var _drained: Array[String] = []


async func _job(p_tag: String) -> String:
	await go
	return "done:" + p_tag


async func _drain(p_handle: Coroutine[String]) -> void:
	_drained.append(await p_handle)


func test() -> void:
	var tags: Array[String] = ["a", "b", "c"]
	var jobs: Array[Coroutine[String]] = []
	for tag: String in tags:
		jobs.append(_job(tag))
	# Every job parked at `await go`, so the array holds three live handles and no result exists yet.
	print("held handles: ", jobs.size())

	for job: Coroutine[String] in jobs:
		_drain(job)
	print("before resume: ", _drained)

	# A single emit releases every suspended job in start order; each completion resumes its runner.
	go.emit()
	print("after resume: ", _drained)
