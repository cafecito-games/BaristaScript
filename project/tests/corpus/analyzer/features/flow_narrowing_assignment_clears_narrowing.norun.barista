extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

# Assigning to a narrowed identifier clears the narrowed type for later reads.
func test(value: Variant, other: Variant) -> void:
	if value is Damageable:
		value.take_damage(1)
		value = other
		value.take_damage(2)
