extends RefCounted
uses Counter

trait Counter:
	var count: int = 5
	func increment() -> void:
		count += 1
	func current() -> int:
		return count

func test() -> void:
	print(count)
	increment()
	increment()
	print(count)
	print(current())
