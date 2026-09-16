# A named tuple's `Self` field admits only the receiver itself on the receiver-relative spellings:
# a foreign value merely typed as the declaring class or a subclass is rejected. The class-handle
# spelling keeps its substituted diagnostic naming the class.
class Receiver:
	tuple Pair(index: int, owner: Self)

	func construct_foreign(foreign: Receiver, sub_value: Sub) -> void:
		var first := Pair(2, foreign)
		var second := Pair(3, sub_value)
		print(first, second)


class Sub:
	extends Receiver


func test() -> void:
	var bad := Receiver.Pair(1, "nope")
	print(bad)
