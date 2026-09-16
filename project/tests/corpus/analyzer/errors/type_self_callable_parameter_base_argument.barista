class Base:
	func copy_from(other: Self) -> void:
		pass


class Child:
	extends Base

	func copy_from(other: Child) -> void:
		pass


func test() -> void:
	var base: Base = Child.new()
	var cb := base.copy_from
	cb.call(Base.new())
