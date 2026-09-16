extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

class Implementer:
	uses Damageable

	func take_damage(_amount: int) -> void:
		pass

func test() -> void:
	var _damageable: Damageable = Implementer
