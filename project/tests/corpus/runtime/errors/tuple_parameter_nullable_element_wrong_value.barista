# A nullable element accepts null or a value of its type. A wrong non-null is neither.
class Receiver extends RefCounted:
	func take(pair: (int, String?)) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, null))
	callback.call((1, 9))
	print("unreachable")
