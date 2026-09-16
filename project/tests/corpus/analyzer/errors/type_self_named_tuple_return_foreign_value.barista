# A returned named tuple whose `Self` field was bound at construction contrasts identically
# against the declared return type; the clause names the binding that differs.
class Receiver:
	tuple Pair(index: int, owner: Self)

	func remake(pair: Pair, other: Receiver) -> Pair:
		return other.Pair(pair.index, other)


func test() -> void:
	pass
