# A raw container alternative accepts a literal as written, so it claims the literal's form just as a
# typed one does. Two alternatives claiming the same form name neither, and the literal is left as it
# was reduced rather than forced through the `Self`-bearing one and reported against its element type.
class Receiver:
	func absorb(entries: Array | Array[Self]) -> String:
		return str(entries)

	func drive() -> void:
		print("ints ", absorb([1]))


func test() -> void:
	Receiver.new().drive()
