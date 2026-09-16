# A blank `final static var` assigned in `_static_init()` is fully initialized by
# the time an instance is constructed, so a non-static member initializer may read
# it freely.
final static var BASE: int

static func _static_init() -> void:
	BASE = 10

var derived := BASE + 1

func test() -> void:
	print(derived)
