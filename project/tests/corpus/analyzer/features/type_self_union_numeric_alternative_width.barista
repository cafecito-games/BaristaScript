# A numeric alternative of a `Self`-bearing union admits the values it would admit standing in a union
# of its own: a widening that keeps the value's carrier costs nothing at run time, and a union slot
# stores the value unchanged.
class Receiver:
	func take(value: long | (int, Self)) -> String:
		return str(value)

	func drive() -> void:
		print("widened ", take(5))


func test() -> void:
	Receiver.new().drive()
