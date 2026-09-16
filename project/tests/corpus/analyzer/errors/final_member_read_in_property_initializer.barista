# A property variable's initializer also runs before the `_init` body, so reading a
# still-blank final from it is a use-before-assignment.
final var id: int
var copy := id:
	get:
		return copy

func _init() -> void:
	id = 1

func test() -> void:
	pass
