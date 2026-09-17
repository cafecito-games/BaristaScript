extends RefCounted
uses Left, Right

# A diamond where the shared base trait contributes a *concrete* method body.
# The method must be flattened exactly once and dispatch correctly, even though
# it is reached through two paths (Left and Right).
trait Base:
	func describe() -> String:
		return "base"

trait Left uses Base:
	pass

trait Right uses Base:
	pass

func test() -> void:
	print(describe())
