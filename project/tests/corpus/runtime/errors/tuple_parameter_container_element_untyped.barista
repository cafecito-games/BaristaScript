# A tuple store never converts, so a container element has to arrive already typed, and the parameter
# boundary enforces that rule like every other position does. A literal written at the call site is
# built from the parameter's declared type and passes; an untyped `Array` that arrives through a
# `Variant` supplier is not being built against anything, so it is still rejected here.
class Receiver extends RefCounted:
	func take(pair: (int, Array[int])) -> void:
		print("took ", pair)


func supply_untyped() -> Variant:
	return [1, 2]


func test() -> void:
	var typed: Array[int] = [1, 2]
	Receiver.new().take((1, typed))
	Receiver.new().take((1, []))
	Receiver.new().take((1, supply_untyped()))
	print("unreachable")
