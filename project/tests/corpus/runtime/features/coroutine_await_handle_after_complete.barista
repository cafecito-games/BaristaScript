# Regression for #1944 (await-after-complete case): a coroutine that genuinely suspended is resumed
# and completes *before* anything awaits its handle. The first `await` of that already-resolved handle
# must resolve immediately with the latched result instead of parking forever on a `completed` signal
# that already fired.
signal go


async func _job(p_value: int) -> String:
	await go
	return "value:" + str(p_value)


async func _await_after(p_handle: Coroutine[String]) -> void:
	print("awaited after complete: ", await p_handle)


func test() -> void:
	var handle: Coroutine[String] = _job(7)
	# Nothing is awaiting yet: resume and complete the job so its result latches on the handle.
	go.emit()
	print("completed before await")

	# The first (and only) await happens after the coroutine already finished, so it must resolve
	# immediately rather than hang.
	_await_after(handle)
