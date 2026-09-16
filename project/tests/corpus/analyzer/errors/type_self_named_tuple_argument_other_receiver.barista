# A named tuple with a `Self` field renders only its declared name, so the receiver-identity
# rejection used to read "should be "Pair" but is "Pair""; the clause names the `Self` binding
# that actually differs.
class Receiver:
	tuple Pair(index: int, owner: Self)

	func take_pair(_pair: Pair) -> void:
		pass


func test() -> void:
	var receiver := Receiver.new()
	var other := Receiver.new()
	var pair := other.Pair(1, other)
	receiver.take_pair(pair)
