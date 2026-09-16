# An unrelated class in a `Self` element position is rejected for an open receiver and for a static
# receiver alike, the static form naming the class `Self` was substituted to.
class Receiver:
	func take_pair(_pair: (int, Self)) -> void:
		pass

	static func take_pair_static(_pair: (int, Self)) -> void:
		pass


class Foreign:
	pass


func test() -> void:
	var receiver := Receiver.new()
	var foreign := Foreign.new()
	receiver.take_pair((1, foreign))
	Receiver.take_pair_static((2, foreign))
