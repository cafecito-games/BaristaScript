extends RefCounted
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

func accept_damageable(value: Damageable) -> Damageable:
	var typed: Damageable = value
	var values: Array[Damageable] = [typed]
	return values[0]

func test(value: Variant) -> void:
	if value is Damageable:
		var casted: Damageable = value
		var _accepted := accept_damageable(casted)
