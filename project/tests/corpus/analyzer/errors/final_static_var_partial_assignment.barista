# Assigning a blank static final on only one arm of an `if` leaves it not
# definitely assigned after `_static_init()`.
final static var VALUE: int

static func _flag() -> bool:
	return false

static func _static_init() -> void:
	if _flag():
		VALUE = 1

func test() -> void:
	pass
