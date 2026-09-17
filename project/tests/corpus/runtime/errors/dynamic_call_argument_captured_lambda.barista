# A lambda receives its captures as the compiled function's leading parameters and re-bases a rejected
# argument's position past them, so the parameter a diagnostic names has to be counted the same way:
# the one the caller wrote, not the capture that precedes it.
func test() -> void:
	var captured: String = "kept"
	var callback := func(value: int) -> void:
		print(captured, " ", value)
	callback.call(1)
	callback.call([1])
	print("unreachable")
