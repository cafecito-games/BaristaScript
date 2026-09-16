# Receiver identity is carried into a union's `Self`-bearing alternative unchanged: the tuple literal
# names this frame's receiver, which is not the receiver the call dispatches on.
class Receiver:
	func attach(_link: int | (int, Self)) -> void:
		pass

	func attach_carrier(_link: int | (int, Array[Self])) -> void:
		pass

	func forward(other: Receiver) -> void:
		other.attach((3, self))
		other.attach_carrier((4, [self]))


func test() -> void:
	Receiver.new().forward(Receiver.new())
