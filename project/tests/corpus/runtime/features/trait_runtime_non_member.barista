extends RefCounted
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(_amount: int) -> void:
	pass

# Structurally matches Damageable but does not apply it.
class Lookalike:
	func take_damage(_amount: int) -> void:
		pass

func test() -> void:
	var implementer: Variant = self
	var lookalike: Variant = Lookalike.new()
	# Nominal membership: only the applier is a Damageable at runtime.
	print(implementer is Damageable)
	print(lookalike is Damageable)
	# `as` returns the typed value on success and null on failure.
	print((implementer as Damageable) != null)
	print((lookalike as Damageable) == null)
