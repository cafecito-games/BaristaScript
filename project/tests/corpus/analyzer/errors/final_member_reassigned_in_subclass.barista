# A subclass cannot reassign a final declared in its base class; the base owns the
# single assignment slot.
class Base:
	final var id := 1

class Derived extends Base:
	func reset() -> void:
		id = 2

func test() -> void:
	pass
