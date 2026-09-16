func accept(index: int, ...names: Array[String]) -> bool:
	return index == names.size()

func test() -> void:
	var callback: Callable[[int, ...Array[String]], bool] = accept
	var bound := callback.bind(7)
	print(bound.call())
	var tail_bound := callback.bind("tail")
	print(tail_bound.call(2, "a"))
