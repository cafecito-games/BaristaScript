# A tuple parameter means the declared shape, not the erased Array carrier it lowers to. The argument
# reaches the callee as a `Variant` through an opaque `Callable`, so the analyzer has no static
# evidence about it and the parameter boundary is the only line of defense.
class Receiver extends RefCounted:
	func take(pair: (int, String)) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, "one"))
	callback.call((1, RefCounted.new()))
	print("unreachable")
