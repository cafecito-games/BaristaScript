extends RefCounted

trait TraitA:
	func collide(value: int) -> int:
		return value

trait TraitB:
	func collide(value: int) -> int:
		return value

# Abstract classes and composing traits may defer disambiguation to a concrete
# subclass, so an unresolved trait method conflict is not an error for them.
abstract class AbstractUser:
	uses TraitA, TraitB

trait CompositeTrait uses TraitA, TraitB:
	pass

func test() -> void:
	print("ok")
