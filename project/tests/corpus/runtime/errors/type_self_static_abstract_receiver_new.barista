# `Self.new()` in a static frame constructs the *receiver's* runtime class, which analysis cannot pin
# down: the declaring class is concrete, so the static abstract-construction gate has nothing to reject.
# An abstract receiver therefore has to be refused at runtime, before an instance of it exists.


class Base:
	static func make() -> Self:
		return Self.new()


abstract class Derived extends Base:
	pass


func test() -> void:
	print("constructed: %s" % [Derived.make()])
