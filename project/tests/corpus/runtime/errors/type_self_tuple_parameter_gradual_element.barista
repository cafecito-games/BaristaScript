# A gradual value in a typed non-`Self` position is left to the run time, exactly as it is for a tuple
# parameter that mentions no `Self` at all: identity settles the `Self` position only, and the store
# still enforces every other element.
class Receiver:
	func take_pair(_pair: (int, Self)) -> void:
		pass


func supply(value: Variant) -> Variant:
	return value


func test() -> void:
	var receiver := Receiver.new()
	receiver.take_pair((supply("not an int"), receiver))
