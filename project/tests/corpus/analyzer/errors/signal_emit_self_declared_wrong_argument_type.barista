# A self-declared signal's `emit()` validates argument types against its declared parameter
# types, exactly like the `Object.emit_signal()` and `connect()` spellings already do. Issue #1690.
signal reported(total: int)


func test() -> void:
	reported.emit("nope")
