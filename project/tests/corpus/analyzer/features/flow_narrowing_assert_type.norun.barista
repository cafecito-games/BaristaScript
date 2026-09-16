extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

func test(value: Variant) -> void:
	assert(value is Damageable)
	value.take_damage(1)
