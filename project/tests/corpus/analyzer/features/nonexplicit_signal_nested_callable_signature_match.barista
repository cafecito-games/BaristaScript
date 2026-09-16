# A locally declared signal flows through the non-explicit Signal path, but when its nested Callable
# parameter signature matches the annotated target the assignment is still accepted: the deepened
# comparison only rejects genuine mismatches, it does not over-tighten compatible signatures.
signal event(cb: Callable[[int], void])


func test() -> void:
	var typed_event: Signal[[Callable[[int], void]]] = event
	print(typed_event != null)
