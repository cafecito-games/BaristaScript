# A static method reached through a `Callable` binds its parameters through the same boundary.
class Receiver extends RefCounted:
	static func take(pair: (int, String)) -> void:
		print("took ", pair)


func test() -> void:
	var callback: Callable = Receiver.take
	callback.call((1, 2, 3))
	print("unreachable")
