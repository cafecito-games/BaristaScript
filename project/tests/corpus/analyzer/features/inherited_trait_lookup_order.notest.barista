trait BaseChoice:
	func choose() -> int:
		return 1


trait DerivedChoice:
	func choose() -> String:
		return "derived"


class TraitBase uses BaseChoice:
	pass


class TraitChild extends TraitBase uses DerivedChoice:
	pass


trait First:
	var selected: int


trait Second:
	var selected: String


abstract class Ordered uses First, Second:
	func read_first() -> int:
		return selected


trait Blocked:
	func blocked() -> int:
		return 2


class DeclaringBase:
	func blocked() -> int:
		return 1


class DeclaringChild extends DeclaringBase uses Blocked:
	pass


func test() -> void:
	var derived_receiver: TraitChild
	var derived: String = derived_receiver.choose()
	var blocking_receiver: DeclaringChild
	var blocked_callable := blocking_receiver.blocked
