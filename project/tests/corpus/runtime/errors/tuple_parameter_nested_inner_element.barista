# An inner element type is enforced at the same depth its declaration sits at.
class Receiver extends RefCounted:
	func take(pair: (int, (String, bool))) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, ("four", 9)))
	print("unreachable")
