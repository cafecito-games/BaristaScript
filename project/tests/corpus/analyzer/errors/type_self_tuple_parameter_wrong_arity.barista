# Identity in the `Self` position settles that position only. The tuple's arity and every non-`Self`
# position are still checked by the ordinary rules.
class Receiver:
	func take_pair(_pair: (int, Self)) -> void:
		pass


func test() -> void:
	var receiver := Receiver.new()
	receiver.take_pair((1, 2, 3))
	receiver.take_pair(("a", receiver))
