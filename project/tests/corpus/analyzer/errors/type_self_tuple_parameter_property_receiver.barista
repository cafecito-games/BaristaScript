# The receiver reference is matched syntactically, and only `self` and a plain name are recognized. A
# receiver reached through a property is not identity-provable at the call site, so repeating its
# spelling in the `Self` position does not admit the argument.
class Receiver:
	func take_pair(_pair: (int, Self)) -> void:
		pass


class Owner:
	var part := Receiver.new()


func test() -> void:
	var owner := Owner.new()
	owner.part.take_pair((1, owner.part))
