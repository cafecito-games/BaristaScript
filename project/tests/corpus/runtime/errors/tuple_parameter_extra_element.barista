# Arity is part of a tuple type, so a longer Array is not a value of it even though its carrier
# matches and its declared prefix does.
class Receiver extends RefCounted:
	func take(pair: (int, String)) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, 2, 3))
	print("unreachable")
