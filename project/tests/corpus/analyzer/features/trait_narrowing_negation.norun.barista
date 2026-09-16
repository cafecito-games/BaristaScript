extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

# `not` inverts which branch narrows: the `else` of a negated trait test narrows
# the identifier to the trait type, so member access type-checks there.
func negated_else(value: Variant) -> void:
	if not value is Damageable:
		print("not damageable")
	else:
		value.take_damage(1)
