# Assigning a blank `final var` local on only one arm of an `if` (no `else`)
# leaves it not definitely assigned afterwards.
func _flag() -> bool:
	return false

func test() -> void:
	final var x: int
	if _flag():
		x = 1
	print(x)
