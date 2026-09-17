# The arguments the analyzer admits by identity are the ones the run time accepts: a tuple parameter
# resolves its `Self` element against the running instance's leaf script, and the element the caller
# passed *is* that instance, so the store passes and the body observes the same object -- for an
# exact-type receiver and for a subclass leaf reached through a base-typed reference alike.
class Receiver:
	func take_pair(pair: (int, Self)) -> void:
		print(pair.0, " same=", pair.1 == self, " leaf=", pair.1.leaf_name())

	func leaf_name() -> String:
		return "Receiver"

	func unqualified_caller() -> void:
		take_pair((2, self))


class Sub:
	extends Receiver

	func leaf_name() -> String:
		return "Sub"


func test() -> void:
	var receiver := Receiver.new()
	receiver.take_pair((1, receiver))
	receiver.unqualified_caller()

	var widened: Receiver = Sub.new()
	widened.take_pair((3, widened))
	widened.unqualified_caller()
