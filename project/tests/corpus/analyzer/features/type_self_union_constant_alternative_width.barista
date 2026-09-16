# A numeric alternative that names no `Self` keeps the constant-sensitive width rules it would keep
# standing in a union of its own: a constant that fits the alternative's declared width reaches it
# without changing the value's carrier, so the union slot stores it unchanged.
class Receiver:
	func take_int(value: int | (int, Self)) -> String:
		return str(value)

	func take_uint(value: uint | (int, Self)) -> String:
		return str(value)

	func drive() -> void:
		print("narrowed ", take_int(5L))
		print("unsigned ", take_uint(5UL))


func test() -> void:
	Receiver.new().drive()
