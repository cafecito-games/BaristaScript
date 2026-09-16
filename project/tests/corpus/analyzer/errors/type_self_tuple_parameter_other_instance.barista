# A different instance of the receiver's own static class is not the receiver, so it proves nothing
# about the runtime leaf `Self` resolves to and stays rejected.
class Receiver:
	func take_pair(_pair: (int, Self)) -> void:
		pass


func test() -> void:
	var receiver := Receiver.new()
	var other := Receiver.new()
	receiver.take_pair((1, other))
