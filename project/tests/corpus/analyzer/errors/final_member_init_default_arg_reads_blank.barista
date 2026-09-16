# A `_init` parameter default is evaluated before the body assigns the blank
# final, so reading it through an omitted default is use-before-assignment.
final var id: int

func _init(copy: int = id) -> void:
	id = copy

func test() -> void:
	pass
