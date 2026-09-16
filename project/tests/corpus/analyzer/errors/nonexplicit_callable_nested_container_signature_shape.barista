# The nested-signature comparison reaches through typed-container element types: a function reference
# whose nested Callable parameter is `Array[Array[String]]` cannot satisfy a target whose nested
# Callable parameter is `Array[Array[int]]`, even though every outer Callable and Array shape matches.
func takes_string_grid_cb(_cb: Callable[[Array[Array[String]]], void]) -> void:
	pass


func test() -> void:
	var handler: Callable[[Callable[[Array[Array[int]]], void]], void] = takes_string_grid_cb
	print(handler)
