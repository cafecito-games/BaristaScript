# A rest element declared as a tuple is validated per element against the same shape. The collected
# array stays untyped, because a tuple carrier is untyped by definition and no typed-container element
# type could describe it.
class Receiver extends RefCounted:
	func take(...rest: Array[(int, String)]) -> void:
		print("took ", rest)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, "one"), (2, "two"))
	callback.call((1, "one"), (2, 3, 4))
	print("unreachable")
