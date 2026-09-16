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

func inspect(value: Variant) -> void:
	if value is Damageable:
		var amount: int = value.id()
		value.take_damage(amount)
