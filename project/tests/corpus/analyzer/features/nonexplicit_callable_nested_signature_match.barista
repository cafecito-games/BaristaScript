# A function reference flows through the non-explicit Callable path, but when its nested Callable
# parameter signature matches the annotated target the assignment is still accepted: the deepened
# comparison only rejects genuine mismatches, it does not over-tighten compatible signatures.
func takes_int_cb(_cb: Callable[[int], void]) -> void:
	pass


func test() -> void:
	var handler: Callable[[Callable[[int], void]], void] = takes_int_cb
	print(handler != null)
