# A converting gradual store is checked on the conversion's result, not its source: truncating 1e18
# produces a well-formed `INT`-carrier value that the declared `int` still cannot hold.
func test() -> void:
	var v: Variant = 1e18
	print("before store")
	var _i: int = v
	print("unreachable")
