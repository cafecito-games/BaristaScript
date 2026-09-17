extends RefCounted
uses TraitA, TraitB

# Two traits each define a *concrete* method with the same name. The collision
# is resolved by the consuming class overriding it; the override is what
# dispatches at runtime, and it may delegate to a specific trait's behavior.
trait TraitA:
	func label() -> String:
		return "A"

trait TraitB:
	func label() -> String:
		return "B"

func label() -> String:
	return "override"

func test() -> void:
	print(label())
