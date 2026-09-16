# Constructing a payload case through an instance base keeps its `Self` field receiver-relative to
# that base expression: the base itself is admitted in every `Self` position, including a receiver
# whose static type widens a subclass instance.
class Receiver:
	enum Message:
		Detach
		Attach(index: int, owner: Self)


class Sub:
	extends Receiver


func test() -> void:
	var receiver := Receiver.new()
	var made := receiver.Message.Attach(8, receiver)
	if made is Receiver.Message.Attach(index, owner):
		prints("receiver", index, owner == receiver)
	var widened: Receiver = Sub.new()
	var widened_case := widened.Message.Attach(9, widened)
	if widened_case is Receiver.Message.Attach(index, owner):
		prints("widened", index, owner == widened)
