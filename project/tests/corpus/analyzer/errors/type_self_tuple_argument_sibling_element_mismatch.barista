# A sibling element's ordinary mismatch fails the whole-type comparison, which would otherwise hide
# the receiver-relative `Self` sitting beside it: fixing the sibling would surface a second, never
# stated rejection. The clause names the slot the `Self` requirement belongs to, reaching through a
# nested carrier as well, while a whole-type match still reports the whole type.
class Receiver:
	func take(pair: (int, Self)) -> void:
		prints(pair.0, pair.1 == self)

	func take_nested(items: Array[(int, Self)]) -> void:
		prints(items.size())

	func route(other: Receiver) -> void:
		other.take((1, self))
		other.take(("x", self))

	func route_nested(other: Receiver) -> void:
		var wrong: Array[(String, Self)] = [("z", self)]
		other.take_nested(wrong)


func test() -> void:
	Receiver.new().route(Receiver.new())
