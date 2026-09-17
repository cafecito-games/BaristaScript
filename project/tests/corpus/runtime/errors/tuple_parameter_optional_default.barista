# An optional parameter is only optional at the call site; a supplied argument is validated like any
# other. The defaulted call goes first, proving the parameter is otherwise callable.
class Receiver extends RefCounted:
	func take(pair: (int, String) = (0, "zero")) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call()
	callback.call((1, 2, 3))
	print("unreachable")
