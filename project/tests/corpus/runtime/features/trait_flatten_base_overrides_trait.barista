extends RefCounted

class Base:
	func label() -> int:
		return 1

class Derived extends Base:
	uses Mixin

	func resolved() -> int:
		return label()

trait Mixin:
	func label() -> int:
		return 2

func test() -> void:
	var d := Derived.new()
	print(d.resolved())
	print(d.label())
