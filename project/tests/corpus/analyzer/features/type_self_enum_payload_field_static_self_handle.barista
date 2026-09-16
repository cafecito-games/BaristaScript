# A static frame constructing through the `Self.Message` spelling has no instance to prove identity
# against, so the payload field stays `Self` -- and an inherited declaration's field compares as the
# frame class's own `Self`, admitting a `Self`-typed parameter of the same frame.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver

	static func construct_static(value: Self) -> void:
		var made := Self.Message.Attach(1, value)
		if made is Receiver.Message.Attach(index, owner):
			prints("static self handle", index, owner == value)


func test() -> void:
	Sub.construct_static(Sub.new())
