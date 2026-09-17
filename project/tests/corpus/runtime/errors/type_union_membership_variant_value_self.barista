# The same answer when every alternative names `Self`.
class Box:
	var label = "box"


class Receiver:
	func take(v: (int, Self) | (String, Self)) -> void:
		print("took ", v)

	func drive(loose: Variant) -> void:
		take(loose)


func test() -> void:
	Receiver.new().drive(Box.new())
