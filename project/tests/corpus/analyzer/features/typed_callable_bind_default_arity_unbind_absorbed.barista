# A bound value that a preceding unbind() fully absorbs never reaches the target at any call
# arity, so it cannot prove a mismatch against a parameter it never fills. The default-shift check
# must skip it entirely rather than validating it against whatever parameter the shift happens to
# land on: here the absorbed value's type (String) would conflict with the shifted-into parameter
# (int) if it were checked, but since unbind(1) discards it before it reaches the base, the base's
# own trailing default still independently widens the result down to a single-argument call.
extends RefCounted


func add_with_default(a: int, b: int = 5) -> int:
	return a + b


func test() -> void:
	var bound := Callable(self, "add_with_default").unbind(1).bind("discarded")
	print(bound.call(3))
