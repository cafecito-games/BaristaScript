extends RefCounted
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

func accept_damageable(value: Damageable) -> Damageable:
	return value

func test() -> void:
	var typed: Damageable = self
	print(typed != null)

	var values: Array[Damageable] = [self]
	print(values.size())
	print(values[0] != null)

	var returned := accept_damageable(self)
	print(returned != null)
