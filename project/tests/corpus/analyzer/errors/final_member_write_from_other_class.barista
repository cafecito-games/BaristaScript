# A final can only be written in its declaring class's `_init`; an unrelated class
# cannot write it through a typed reference.
class Box:
	final var id: int

	func _init(p_id: int) -> void:
		id = p_id

class Factory:
	func build() -> Box:
		var box := Box.new(1)
		box.id = 2
		return box

func test() -> void:
	pass
