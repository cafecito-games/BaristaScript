extends RefCounted
uses Damageable, Trackable

trait Identified:
	abstract func id() -> int

trait Damageable uses Identified:
	pass

trait Trackable uses Identified:
	pass

func id() -> int:
	return 1

func test() -> void:
	print("ok")
