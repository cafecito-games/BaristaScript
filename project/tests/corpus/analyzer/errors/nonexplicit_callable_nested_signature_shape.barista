# A function reference flows through the non-explicit Callable path: its rich parameter/return
# signature is populated from the function node without `has_explicit_method_signature`. A nested
# Callable parameter mismatch must still be detected, so a function whose parameter is
# `Callable[[String], void]` cannot be assigned to a variable annotated
# `Callable[[Callable[[int], void]], void]`, even though the outer Callable shapes are identical.
func takes_string_cb(_cb: Callable[[String], void]) -> void:
	pass


func test() -> void:
	var handler: Callable[[Callable[[int], void]], void] = takes_string_cb
	print(handler)
