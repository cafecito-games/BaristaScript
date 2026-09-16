# A literal whose shape two alternatives could claim is left as it was reduced rather than built
# against one of them: committing it to the `Self`-bearing alternative would report an element the
# other alternative accepts, before the union check ever gets to answer.
class Receiver:
	func absorb(entries: Array[int] | Array[Self]) -> String:
		return str(entries)

	func drive() -> void:
		print("ints ", absorb([1]))


func test() -> void:
	Receiver.new().drive()
