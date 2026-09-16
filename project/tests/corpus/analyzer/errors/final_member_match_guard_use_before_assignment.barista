# A `match` guard is evaluated before its branch body, so reading a blank final
# from the guard is a use-before-assignment.
final var id: int

func _init(value: int) -> void:
	match value:
		_ when id > 0:
			id = 1
		_:
			id = 2

func test() -> void:
	pass
