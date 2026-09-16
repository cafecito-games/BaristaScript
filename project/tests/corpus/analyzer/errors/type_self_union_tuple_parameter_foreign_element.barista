# A type union is a set of alternatives, so a `Self`-bearing alternative keeps the rules it would
# have standing alone: an unrelated value and a value typed as the bound are both rejected in the
# tuple's `Self` element, exactly as a bare `(int, Self)` parameter rejects them.
class Foreign:
	pass


class Receiver:
	func attach(_link: int | (int, Self)) -> void:
		pass


func test() -> void:
	var receiver := Receiver.new()
	var foreign := Foreign.new()
	var stranger := Receiver.new()
	receiver.attach((1, foreign))
	receiver.attach((2, stranger))
