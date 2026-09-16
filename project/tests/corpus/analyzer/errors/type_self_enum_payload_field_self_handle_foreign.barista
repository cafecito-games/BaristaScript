# The `Self.Message` spelling names the running receiver's class, so its payload `Self` fields stay
# relative to the calling frame's receiver: `self` is admitted, a foreign instance is rejected.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)

	func construct_via_self_handle(other: Receiver) -> void:
		var good := Self.Message.Attach(1, self)
		var bad := Self.Message.Attach(2, other)
		print(good, bad)


func test() -> void:
	pass
