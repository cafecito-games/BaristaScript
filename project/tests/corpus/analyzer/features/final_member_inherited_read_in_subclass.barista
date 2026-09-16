# A subclass may freely read an inherited final; only writing it is forbidden.
class Base:
	final var id := 7

class Derived extends Base:
	func doubled() -> int:
		return id * 2

func test() -> void:
	var derived := Derived.new()
	print(derived.id)
	print(derived.doubled())
