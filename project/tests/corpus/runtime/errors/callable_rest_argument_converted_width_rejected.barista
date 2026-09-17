# A typed rest parameter converts each collected argument through the same boundary as a fixed one,
# so an element whose truncated magnitude the declared element width cannot hold is rejected rather
# than collected into the array.
func take(...values: Array[int]) -> void:
	print(values)


func test() -> void:
	var callback: Callable = take
	callback.call(1.9, -1.9)
	callback.call(1.9, 2147483648.0)
	print("unreachable")
