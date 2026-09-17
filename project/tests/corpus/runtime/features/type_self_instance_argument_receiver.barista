# An inherited instance method validates its `Self` parameter against the receiver's leaf script, not
# the class the member was lowered against. Routed through `Object::call` so the analyzer cannot mask
# the runtime behavior, a `Sibling` is rejected for a `Self` parameter on a `Child` receiver, and the
# diagnostic identifies `Child` -- not `Base`, `Self`, or `Variant`.
class Base:
	func consume(other: Self) -> void:
		print("accepted: ", other.get_script() == Sibling)


class Child extends Base:
	pass


class Sibling extends Base:
	pass


func test() -> void:
	var obj: Object = Child.new()
	obj.call("consume", Sibling.new())
	print("no runtime error raised")
