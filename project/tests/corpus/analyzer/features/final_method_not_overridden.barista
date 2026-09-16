# A final method that is never overridden compiles and runs normally, and a
# non-final sibling method on the same base may still be overridden.
class Base:
	final func locked() -> String:
		return "locked"

	func open() -> String:
		return "base-open"

class Derived extends Base:
	func open() -> String:
		return "derived-open"

func test():
	var instance := Derived.new()
	print(instance.locked())
	print(instance.open())
