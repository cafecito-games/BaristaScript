# `Self` in parameter position is satisfied by identity at every nesting depth a value travels
# through: on an open receiver `Self` means the runtime leaf, and an argument that *is* the receiver
# is an instance of that leaf whatever it turns out to be. A tuple element position carries the rule
# inward, so `(int, Self)` admits a literal whose second element is the receiver reference the call
# is made through -- unqualified, `self.`, `super.`, an instance receiver, or a trait-typed one.
trait ZzPairable:
	func take_tuple(pair: (int, Self)) -> void:
		print("trait ", pair.0)


class Receiver:
	uses ZzPairable

	func take_pair(pair: (int, Self)) -> void:
		print("pair ", pair.0)

	func unqualified_caller() -> void:
		take_pair((1, self))

	func explicit_self_caller() -> void:
		self.take_pair((2, self))


class Sub:
	extends Receiver

	func super_caller() -> void:
		super.take_pair((3, self))


func test() -> void:
	var receiver := Receiver.new()
	receiver.take_pair((4, receiver))
	receiver.unqualified_caller()
	receiver.explicit_self_caller()
	Sub.new().super_caller()

	var conforming: ZzPairable = Receiver.new()
	conforming.take_tuple((5, conforming))
