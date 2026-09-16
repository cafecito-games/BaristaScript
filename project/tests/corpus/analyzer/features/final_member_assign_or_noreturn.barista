# A branch that ends in a `@noreturn` call (here `push_fatal`) is an unreachable
# join path, so the final is still definitely assigned afterwards.
class Validated:
	final var id: int

	func _init(valid: bool) -> void:
		if valid:
			id = 1
		else:
			push_fatal("invalid")
		print("id is %d" % id)

func test() -> void:
	var _validated := Validated.new(true)
