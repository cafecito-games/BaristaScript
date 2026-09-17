# Nullability admits null, nothing else: a wrong non-null value is rejected exactly as it would be in
# a non-nullable slot. The accepted null goes first.
class Receiver extends RefCounted:
	func take(pair: (int, String)?) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call(null)
	callback.call((1, 2, 3))
	print("unreachable")
