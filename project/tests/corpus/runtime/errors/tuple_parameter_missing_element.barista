# A shorter Array is rejected for the same reason a longer one is: the arity is declared.
class Receiver extends RefCounted:
	func take(pair: (int, String)) -> void:
		print("took ", pair)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call([1])
	print("unreachable")
