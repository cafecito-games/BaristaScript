# A member initializer runs in the constructor prologue, so a lambda created there
# writes the final outside its single legal assignment slot.
final var id: int
var callback := func() -> void: id = 2

func _init() -> void:
	id = 1

func test() -> void:
	pass
