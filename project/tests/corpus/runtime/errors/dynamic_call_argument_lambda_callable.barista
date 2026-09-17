# A lambda has a compiled signature like any other function, so a call through the lambda's callable
# names the lambda's own declared parameter.
func test() -> void:
	var callback := func(values: Array[int]) -> void:
		print("took ", values)
	callback.call([1, 2] as Array[int])
	callback.call(["one"] as Array[String])
	print("unreachable")
