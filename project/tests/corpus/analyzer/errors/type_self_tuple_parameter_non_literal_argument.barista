# Identity has to be provable where the call is written. A tuple-shaped value that arrives through a
# local or through a call result says nothing about this frame's receiver, so neither is admitted even
# though the value it holds is the receiver.
class Receiver:
	func take_pair(_pair: (int, Self)) -> void:
		pass

	func make_pair() -> (int, Receiver):
		return (1, self)


func test() -> void:
	var receiver := Receiver.new()
	var held: (int, Receiver) = (1, receiver)
	receiver.take_pair(held)
	receiver.take_pair(receiver.make_pair())
