# An AsyncCallable signature requires a return type after the parameter list, mirroring Callable.
func test() -> void:
	var bad: AsyncCallable[[int]] = func(_x: int) -> void: pass
	print(bad)
