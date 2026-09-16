# A locally declared signal's rich parameter signature is populated from the declaration itself. A
# nested Callable parameter mismatch must still be detected, so a signal whose parameter is
# `Callable[[String], void]` cannot be assigned to a variable annotated
# `Signal[[Callable[[int], void]]]`, even though the outer Signal shapes are identical.
signal event(cb: Callable[[String], void])


func test() -> void:
	var typed_event: Signal[[Callable[[int], void]]] = event
	print(typed_event)
