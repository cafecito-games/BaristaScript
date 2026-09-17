# The check recurses: an inner tuple is tested as a tuple, so its own arity is enforced too.
class Receiver extends RefCounted:
	func take(pair: (int, (String, bool))) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, ("four", true, 9)))
	print("unreachable")
