# Constructing a named tuple through an instance base answers `Self` identity against the base
# expression, not the calling frame: another instance is rejected, and so is the frame's own `self`.
class Receiver:
	tuple Pair(index: int, owner: Self)

	func construct_via_base(receiver: Receiver, other: Receiver) -> void:
		var first := receiver.Pair(1, other)
		var second := receiver.Pair(2, self)
		print(first, second)


func test() -> void:
	pass
