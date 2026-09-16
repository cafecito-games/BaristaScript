# A blank final assigned exactly once in `_init()` is valid and read-only after.
class Boxed:
	final var id: int

	func _init(p_id: int) -> void:
		id = p_id

func test() -> void:
	var box := Boxed.new(42)
	print(box.id)
