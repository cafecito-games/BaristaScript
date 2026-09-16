# Returning early from `_static_init()` leaves a blank static final unassigned
# for the lifetime of the class, so it must be assigned before every return.
final static var MODE: int

static func _flag() -> bool:
	return false

static func _static_init() -> void:
	if _flag():
		MODE = 1
	else:
		return

func test() -> void:
	pass
