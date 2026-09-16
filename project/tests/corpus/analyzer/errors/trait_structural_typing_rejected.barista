extends RefCounted

trait Damageable:
	abstract func take_damage(amount: int) -> void

# Structurally identical to Damageable but it never applies the trait, so it is
# not a Damageable. Trait identity is nominal, not structural.
class Lookalike:
	func take_damage(_amount: int) -> void:
		pass

func test() -> void:
	var _damageable: Damageable = Lookalike.new()
