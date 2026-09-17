# A wrong-argument direct call renders the receiver's leaf class -- not the literal "Self" token --
# in the diagnostic, the acceptance criterion from #1703's Reproduction C. `Self` on a `Child`
# receiver resolves to `Child`, so a `Sibling` argument is rejected as "should be Child".
class Base:
	func consume(other: Self) -> void:
		pass


class Child extends Base:
	pass


class Sibling extends Base:
	pass


func test() -> void:
	var child := Child.new()
	child.consume(Sibling.new())
