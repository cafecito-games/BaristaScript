# A gradual store carries only a value, so the destination's declared width is checked at the store
# itself: an `INT`-carrier magnitude the declared `int` cannot hold is a runtime error rather than a
# variable that violates its own type.
func test() -> void:
	var v: Variant = 5000000000
	print("before store")
	var _i: int = v
	print("unreachable")
