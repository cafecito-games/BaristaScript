# A direct call to an inherited `Self`-parameter method resolves the parameter to the receiver's
# leaf class in the analyzer's `reduce_call`, the headline diagnostic fix from #1703. Routed as a
# direct call (not through `Object::call`) so the analyzer path -- not the runtime fallback --
# resolves `Self`. A `Child` argument is accepted on a `Child` receiver.
class Base:
	func consume(other: Self) -> void:
		print("accepted: ", other.get_script() == Child)


class Child extends Base:
	pass


func test() -> void:
	var child := Child.new()
	child.consume(Child.new())
