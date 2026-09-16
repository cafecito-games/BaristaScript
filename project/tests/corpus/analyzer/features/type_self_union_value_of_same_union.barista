# A value whose own static type is a union reaches the destination only when every alternative it may
# hold does, since nothing narrows it at the boundary. Passing the parameter straight through to the
# same union therefore holds, with each `Self`-bearing alternative answering receiver identity for
# itself.
class Receiver:
	func attach(link: int | (int, Self)) -> String:
		return str(link)

	func forward(link: int | (int, Self)) -> void:
		print("forwarded ", attach(link))


func test() -> void:
	Receiver.new().forward(9)
