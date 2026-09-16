# A subclass cannot assign a final declared in its base class even from its own
# `_init()`; the base owns the single assignment slot, not the subclass.
class Base:
	final var id := 1

class Derived extends Base:
	func _init() -> void:
		id = 2

func test() -> void:
	pass
