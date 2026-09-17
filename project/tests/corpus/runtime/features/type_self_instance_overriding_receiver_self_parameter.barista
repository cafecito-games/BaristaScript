# An overriding subclass declares the member itself, so `reduce_call`'s inherit-only `Self`
# substitution does not fire and the parameter stays under the receiver-relative contract -- the
# third leg of #1703's analyzer change. A `Self`-typed override on a `Child`-typed receiver keeps
# the literal "Self" diagnostic rather than resolving to "Child" the way an inherited method would.
class Base:
	func consume(other: Self) -> void:
		pass


class Child extends Base:
	func consume(other: Self) -> void:
		pass


class Sibling extends Base:
	pass


func test() -> void:
	var child := Child.new()
	child.consume(Sibling.new())
