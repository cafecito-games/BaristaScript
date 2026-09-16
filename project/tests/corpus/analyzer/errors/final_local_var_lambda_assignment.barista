# A `final var` local captured by a nested lambda cannot be reassigned there; the
# lambda is a separate scope outside the single-assignment slot.
func test() -> void:
	final var x := 1
	var reassign := func() -> void:
		x = 2
	reassign.call()
	print(x)
