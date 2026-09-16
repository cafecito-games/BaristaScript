extends RefCounted
uses Damageable

trait Identified:
	abstract func id() -> int

trait Damageable uses Identified:
	abstract func take_damage(amount: int) -> void

func id() -> int:
	return 1

func take_damage(_amount: int) -> void:
	pass

func test(value: Variant) -> void:
	match value:
		value is Damageable:
			value.take_damage(1)
		_:
			pass
