# Constructing a named tuple through an instance base keeps its `Self` field receiver-relative to
# that base expression: the base itself is admitted in every `Self` position, including a receiver
# whose static type widens a subclass instance.
class Receiver:
	tuple Pair(index: int, owner: Self)


class Sub:
	extends Receiver


func test() -> void:
	var receiver := Receiver.new()
	var made := receiver.Pair(8, receiver)
	print("receiver ", made.index, " ", made.owner == receiver)
	var widened: Receiver = Sub.new()
	var widened_pair := widened.Pair(9, widened)
	print("widened ", widened_pair.index, " ", widened_pair.owner == widened)
