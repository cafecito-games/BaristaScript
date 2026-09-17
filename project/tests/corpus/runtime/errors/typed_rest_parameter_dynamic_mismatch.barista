func collect(...values: Array[int]) -> void:
	print("body ran with ", values)

func test() -> void:
	print("before")
	var callback: Callable = collect
	callback.call("bad")
