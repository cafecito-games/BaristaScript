func gradual_tail(_callback: Callable[[...Array], void]) -> void:
	pass


func typed_tail(_callback: Callable[[...Array[int]], void]) -> void:
	pass


func fixed(_callback: Callable[[int], void]) -> void:
	pass


func test() -> void:
	var wrong: int = 0
	gradual_tail(wrong)
	typed_tail(wrong)
	fixed(wrong)
	var gradual: Callable[[...Array], void] = func(..._values: Array) -> void: pass
	var bad: int = gradual
	print(bad)
