extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

trait Movable:
	abstract func move_by(delta: int) -> void

func take_damage(_amount: int) -> void:
	pass

func move_by(_delta: int) -> void:
	pass

# Two distinct identifiers are narrowed to different traits within the same
# branch.
func multiple(first: Variant, second: Variant) -> void:
	if first is Damageable:
		if second is Movable:
			first.take_damage(1)
			second.move_by(1)

# A nested test re-narrows the inner identifier independently of the outer one.
func nested(value: Variant, other: Variant) -> void:
	if value is Damageable:
		value.take_damage(1)
		if other is Movable:
			other.move_by(1)
		value.take_damage(2)
