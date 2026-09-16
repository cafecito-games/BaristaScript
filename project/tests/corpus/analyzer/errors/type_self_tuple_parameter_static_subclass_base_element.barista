# A static call substitutes `Self` to the exact class the handle names, and tuple elements are
# invariant, so a base-typed value in the `Self` position of a subclass handle's call is rejected
# rather than narrowed.
class Receiver:
	static func take_pair(_pair: (int, Self)) -> void:
		pass


class Sub:
	extends Receiver


func test() -> void:
	Sub.take_pair((1, Receiver.new()))
