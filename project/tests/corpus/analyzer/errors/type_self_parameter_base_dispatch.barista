abstract class Base:
	abstract func copy_from(other: Self) -> void


class Child:
	extends Base

	func copy_from(other: Child) -> void:
		pass


class OtherChild:
	extends Base

	func copy_from(other: OtherChild) -> void:
		pass


func test() -> void:
	var base: Base = Child.new()
	base.copy_from(OtherChild.new())
