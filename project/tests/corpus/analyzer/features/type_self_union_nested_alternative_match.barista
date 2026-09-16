# The deeper alternative comparison only separates alternatives that really differ: a union nested
# beside a `Self` position still matches when its alternatives are the same types, and the plain
# alternative keeps taking its own values.
class Receiver:
	func handle(_value: int) -> void:
		pass

	func take(pair: (int | Callable[[int], void], Self)) -> String:
		return str(pair.1 == self)

	func drive() -> void:
		var matching: int | Callable[[int], void] = handle
		print("matching ", take((matching, self)))
		print("plain ", take((7, self)))


func test() -> void:
	Receiver.new().drive()
