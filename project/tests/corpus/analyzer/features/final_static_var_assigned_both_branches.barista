# A blank static final assigned on every branch of an `if`/`else` in `_static_init()`
# is definitely assigned at the join.
final static var MODE: int

static func _flag() -> bool:
	return false

static func _static_init() -> void:
	if _flag():
		MODE = 1
	else:
		MODE = 2

func test() -> void:
	print(MODE)
