# Writing a final through another instance of the declaring type is not this
# instance's single legal slot (its own declaration or `_init`).
class Box:
	final var id: int

	func _init(p_id: int) -> void:
		id = p_id

	func overwrite(other: Box) -> void:
		other.id = 99

func test() -> void:
	pass
