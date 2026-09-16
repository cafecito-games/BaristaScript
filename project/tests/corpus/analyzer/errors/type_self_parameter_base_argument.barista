class Base:
	func copy_from(other: Self) -> void:
		pass


class Child:
	extends Base

	func copy_from(other: Child) -> void:
		pass


func test() -> void:
	var base: Base = Child.new()
	base.copy_from(Base.new())
