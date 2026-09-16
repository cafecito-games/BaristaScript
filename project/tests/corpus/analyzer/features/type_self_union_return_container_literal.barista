# A union names its container slots on its alternatives rather than on itself, so a returned literal is
# built against the alternative it is written for. `Self` then resolves against the returning frame's
# own receiver, exactly as it does for a plain `Array[Self]` return type.
class Receiver:
	func make() -> Array[Self] | int:
		return [self]

	func make_plain() -> Array[Self] | int:
		return 3

	func drive() -> void:
		var made := make()
		print("made ", made is Array)
		print("plain ", make_plain())


func test() -> void:
	Receiver.new().drive()
