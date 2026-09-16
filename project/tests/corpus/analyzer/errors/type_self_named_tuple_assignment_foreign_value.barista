# Assigning a named tuple whose `Self` field was bound at construction to a variable whose
# specified type still reads `Self` contrasts identically; the clause names the binding.
class Receiver:
	tuple Pair(index: int, owner: Self)

	func stash(other: Receiver) -> void:
		var mine: Pair = other.Pair(1, other)
		print(mine.index)


func test() -> void:
	pass
