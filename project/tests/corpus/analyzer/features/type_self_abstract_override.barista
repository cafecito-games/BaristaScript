abstract class Base:
	abstract func copy_from(other: Self) -> void


class Child:
	extends Base

	func copy_from(other: Child) -> void:
		print(other is Child)


func test() -> void:
	var child := Child.new()
	child.copy_from(child)
	var base: Base = child
	base.copy_from(base)
