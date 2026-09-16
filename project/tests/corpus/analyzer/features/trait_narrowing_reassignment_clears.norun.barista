extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

# Trait narrowing is flow-sensitive: reassigning the narrowed identifier clears
# the narrowed trait type. The first access resolves against Damageable, but the
# access after the reassignment falls back to the declared Variant type and is
# flagged as an unsafe method access.
func test(value: Variant, other: Variant) -> void:
	if value is Damageable:
		value.take_damage(1)
		value = other
		value.take_damage(2)
