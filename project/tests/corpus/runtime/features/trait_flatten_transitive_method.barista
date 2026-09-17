extends RefCounted
uses Trackable

trait Damageable:
	func ping() -> int:
		return 42

trait Trackable uses Damageable:
	pass

func test() -> void:
	print(ping())
