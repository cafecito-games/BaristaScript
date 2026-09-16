# Identity, not the receiver's static type, is what admits the argument: a receiver declared as the
# base but holding a subclass instance passes its own reference, which is the leaf the call resolves
# `Self` to at run time.
class Receiver:
	func take_pair(pair: (int, Self)) -> void:
		print("pair ", pair.0)


class Sub:
	extends Receiver


func test() -> void:
	var widened: Receiver = Sub.new()
	widened.take_pair((1, widened))
