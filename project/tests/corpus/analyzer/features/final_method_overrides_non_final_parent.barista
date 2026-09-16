# A `final func` may itself override a non-final parent method; finality only
# blocks further overriding downstream. A non-final method on a separate branch
# can still be overridden normally.
class Base:
	func describe() -> String:
		return "base"

class Derived extends Base:
	final func describe() -> String:
		return "derived"

func test():
	var instance := Derived.new()
	print(instance.describe())
