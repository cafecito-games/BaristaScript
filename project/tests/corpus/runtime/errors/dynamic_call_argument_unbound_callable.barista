# An unbound callable drops trailing arguments before the callee sees them, so the parameter a
# rejection names is still the callee's own, counted after the drop.
func take(value: int) -> void:
	print("took ", value)


func test() -> void:
	var callback: Callable = take
	var unbound := callback.unbind(1)
	unbound.call(1, "dropped")
	unbound.call([1], "dropped")
	print("unreachable")
