extends RefCounted

class Base:
	func label() -> int:
		return 1

trait Mixin:
	func label() -> String:
		return "mixin"

class Derived extends Base:
	uses Mixin

func test() -> void:
	print(Derived.new().label())
