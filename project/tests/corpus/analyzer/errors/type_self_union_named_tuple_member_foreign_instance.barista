# A named tuple carrying a `Self` field is receiver-relative wherever it stands, including as one
# alternative of a type union, so a pair built against another receiver is rejected there too.
class Receiver:
	tuple Pair(index: int, owner: Self)

	func take(_value: int | Pair) -> void:
		pass


func test() -> void:
	var receiver := Receiver.new()
	var other := Receiver.new()
	receiver.take(other.Pair(1, other))
