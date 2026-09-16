# A tagged-union payload's `Self` field admits only the receiver itself on the receiver-relative
# spellings: a foreign value merely typed as the declaring class or a subclass is rejected, and the
# contextual shorthand answers the same frame. The class-handle spelling keeps its substituted
# diagnostic naming the class.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)

	func construct_foreign(foreign: Receiver, sub_value: Sub) -> void:
		var first := Message.Attach(2, foreign)
		var second := Message.Attach(3, sub_value)
		var shorthand: Message = .Attach(4, foreign)
		print(first, second, shorthand)


class Sub:
	extends Receiver


func test() -> void:
	var bad := Receiver.Message.Attach(1, "nope")
	print(bad)
