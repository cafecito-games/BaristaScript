# A trait element is tested by conformance, so a class that does not apply the trait is rejected.
trait Marker:
	func marked() -> bool:
		return true


class Marked extends RefCounted:
	uses Marker


class Other extends RefCounted:
	pass


class Receiver extends RefCounted:
	func take(pair: (int, Marker)) -> void:
		print("took ", pair != null)


func test() -> void:
	var receiver := Receiver.new()
	var callback: Callable = receiver.take
	callback.call((1, Marked.new()))
	callback.call((1, Other.new()))
	print("unreachable")
