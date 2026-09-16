# A tagged-union payload field spelled as a type union keeps the `Self` rules of its alternatives: an
# unrelated value, a value typed as the bound, and a frame-relative `self` reached through an instance
# base are all rejected in the alternative's `Self` position.
class Foreign:
	pass


class Receiver:
	enum Message:
		Detach
		Attach(link: int | (int, Self))

	func construct_via_base(receiver: Receiver, other: Receiver) -> void:
		var first := receiver.Message.Attach((1, other))
		var second := receiver.Message.Attach((2, self))
		var third := receiver.Message.Attach((3, Foreign.new()))
		print(first, second, third)


func test() -> void:
	pass
