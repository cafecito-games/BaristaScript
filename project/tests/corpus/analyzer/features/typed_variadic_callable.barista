func accept(index: int, ...names: Array[String]) -> bool:
	return index == names.size()

func gradual(index: int, ...values: Array) -> bool:
	return index == values.size()

func test() -> void:
	var callback: Callable[[int, ...Array[String]], bool] = accept
	print(callback.call(2, "a", "b"))
	print(callback.callv([2, "a", "b"]))
	print(callback.bind("tail"))
	print(callback.unbind(1))
	var gradual_callback: Callable[[int, ...Array], bool] = gradual
	print(gradual_callback.call(2, 7, "a", null))
