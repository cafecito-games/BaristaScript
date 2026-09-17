# A lambda's parameters are compiled by the same path a declared function's are.
func test() -> void:
	var take := func(pair: (int, String)) -> void:
		print("took ", pair)
	var callback: Callable = take
	callback.call((1, 2, 3))
	print("unreachable")
