# A lambda nested in an array literal still introduces a new scope, so writing a
# final from it is outside the single legal assignment slot.
final var id: int

func _init() -> void:
	id = 1
	var callbacks := [func() -> void: id = 2]
	callbacks.size()

func test() -> void:
	pass
