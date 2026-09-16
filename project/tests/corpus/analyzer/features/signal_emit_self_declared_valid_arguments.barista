# A self-declared signal's `emit()` call site is validated against its declared parameters, but a
# correctly typed call (including a zero-parameter signal and a signal inherited from a base class,
# accessed without an explicit `self.` prefix) is still accepted. Issue #1690.
signal reported(total: int)
signal reset


func test() -> void:
	reported.emit(1)
	reset.emit()
	print("No failure")
