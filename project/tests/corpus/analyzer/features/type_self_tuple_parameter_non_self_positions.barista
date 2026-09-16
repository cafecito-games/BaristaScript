# Only the `Self` positions of a tuple parameter demand identity. A nullable `Self` element admits
# `null` on its own terms, and a position declared `Variant` takes any value -- gradual ones included
# -- exactly as ordinary argument validation does.
class Receiver:
	func take_optional_element(pair: (int, Self?)) -> void:
		print("optional element ", pair.0, " ", pair.1 == null)

	func take_gradual(pair: (Variant, Self)) -> void:
		print("gradual ", pair.0, " same=", pair.1 == self)


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var receiver := Receiver.new()
	receiver.take_optional_element((1, receiver))
	receiver.take_optional_element((2, null))
	receiver.take_gradual((supply("anything"), receiver))
