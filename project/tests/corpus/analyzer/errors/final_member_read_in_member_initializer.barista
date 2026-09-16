# A member initializer runs before `_init()`, so reading a still-blank final from
# one is a use-before-assignment.
final var id: int
var copy := id

func _init() -> void:
	id = 1

func test() -> void:
	pass
