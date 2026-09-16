# An initialized `final static var` already filled its slot, so assigning it again
# in `_static_init()` is a double assignment.
final static var VALUE := 1

static func _static_init() -> void:
	VALUE = 2

func test() -> void:
	pass
