# Soundness boundary for the held-handle relaxation: capturing a genuinely suspending async call
# into a weakly inferred local (`var x = _job()` with no annotation and no `:=`) does NOT commit to a
# hard `Coroutine[T]` slot, so it is still treated as a probably-forgotten `await` and trips the
# debug missing-await guard at runtime. Only a hard `Coroutine[T]`-typed target exempts the capture.
signal go


async func _job() -> String:
	await go
	return "job-done"


func test() -> void:
	var handle = _job()
	print(handle)
