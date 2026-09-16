# A tagged union flattened from a trait binds its payload `Self` fields to the implementing
# receiver, not the trait: the frame's own `self` is admitted, while another instance of the same
# implementer and a different conforming implementation are both rejected.
trait Messenger:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Impl:
	uses Messenger

	func make(other: Impl, foreign: OtherImpl) -> void:
		var good := Message.Attach(1, self)
		var bad := Message.Attach(2, other)
		var worse := Message.Attach(3, foreign)
		print(good, bad, worse)


class OtherImpl:
	uses Messenger


func test() -> void:
	pass
