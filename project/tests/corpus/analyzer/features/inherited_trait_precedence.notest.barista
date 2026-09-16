trait Named:
	func label() -> String:
		return "trait"


trait Token:
	const TOKEN := 1


class Base uses Named, Token:
	pass


class Child extends Base:
	func label() -> String:
		return "class"


trait Root:
	func diamond() -> String:
		return "root"


trait Left uses Root:
	pass


trait Right uses Root:
	pass


class DiamondBase uses Left, Right:
	pass


class DiamondChild extends DiamondBase:
	pass


class Outer:
	const TOKEN := "outer"

	class Inner extends Base:
		func lexical() -> String:
			return TOKEN

		func lexical_explicit(inner: Inner) -> String:
			return inner.TOKEN


func test() -> void:
	var child: Child
	var shadowed: String = child.label()
	var diamond_child: DiamondChild
	var diamond: String = diamond_child.diamond()
