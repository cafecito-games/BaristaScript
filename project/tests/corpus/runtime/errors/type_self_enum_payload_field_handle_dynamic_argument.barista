# A dynamic argument in a `Self` payload position converts at runtime against the class the exact
# handle spelling named, not the declaration bound: a base-class instance boxed in a Variant is
# rejected when the case is constructed through a subclass handle.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


func test() -> void:
	var boxed: Variant = Receiver.new()
	var made := Sub.Message.Attach(1, boxed)
	print(made is Receiver.Message.Attach)
