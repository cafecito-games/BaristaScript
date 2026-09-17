# The same answer when every alternative names `Self`.
class Box:
	var label = "box"


class Receiver:
	func absorb(entries: Array[Self] | Array[Array[Self]]) -> void:
		print("absorbed ", entries)

	func drive() -> void:
		absorb([Box.new()])


func test() -> void:
	Receiver.new().drive()
