# An alias is not a nominal type, so it cannot be applied as a trait.
trait Describable:
	abstract func describe() -> String


type Only = Describable


class User:
	uses Only

	func describe() -> String:
		return "user"
