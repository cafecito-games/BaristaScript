# The typed-return opcode enforces the declared width exactly as the typed assign does: a gradual
# return cannot hand the caller a value the declared return type could not hold.
func produce() -> int:
	var v: Variant = 5000000000
	print("before return")
	return v


func test() -> void:
	# The error aborts the callee's frame; the caller observes the default value and continues.
	var _i: int = produce()
	print("after failed return")
