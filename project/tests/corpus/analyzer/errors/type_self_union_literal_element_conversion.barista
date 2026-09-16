# A tuple store never converts, and a union slot emits no conversion either, so a literal element that
# would have to change the value's carrier to reach the element type it faces is not the value that
# position declares. When two alternatives could each claim the literal only by converting it, neither
# is the one it was written for, and the literal is refused as it stands -- the answer a union that
# mentions no `Self` already gives, and the answer a single `Self`-bearing tuple gives too.
class Receiver:
	var counter: int = 1

	func take_self(_pair: (bool, Self) | (float, Self)) -> void:
		pass

	func take_plain(_pair: (bool, int) | (float, int)) -> void:
		pass

	func take_single(_pair: (float, Self)) -> void:
		pass

	func drive() -> void:
		take_self((1, self))
		take_plain((1, 2))
		take_single((counter, self))


func test() -> void:
	pass
