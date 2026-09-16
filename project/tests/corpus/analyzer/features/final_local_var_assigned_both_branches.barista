# A blank `final var` local assigned on every arm of an `if`/`else` is definitely
# assigned at the join.
func _flag() -> bool:
	return false

func test() -> void:
	final var size: int
	if _flag():
		size = 1
	else:
		size = 2
	print(size)
