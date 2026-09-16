# A named tuple's field type is checked at construction with the same rules a call parameter uses, so
# a union field's `Self`-bearing alternative demands the constructing receiver in its `Self` position.
class Foreign:
	pass


class Receiver:
	tuple Link(index: int, target: int | (int, Self))


func test() -> void:
	var receiver := Receiver.new()
	var stranger := Receiver.new()
	var foreign := Foreign.new()
	var linked := receiver.Link(0, (1, stranger))
	var alien := receiver.Link(1, (2, foreign))
	print(linked.index, " ", alien.index)
