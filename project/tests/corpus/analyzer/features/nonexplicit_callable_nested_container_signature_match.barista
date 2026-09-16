# Mirror of the nested-container error case with matching element types: when the nested Callable
# parameter is `Array[Array[int]]` on both sides the function reference is accepted, confirming the
# deep container recursion only rejects genuine mismatches.
func takes_int_grid_cb(_cb: Callable[[Array[Array[int]]], void]) -> void:
	pass


func test() -> void:
	var handler: Callable[[Callable[[Array[Array[int]]], void]], void] = takes_int_grid_cb
	print(handler != null)
