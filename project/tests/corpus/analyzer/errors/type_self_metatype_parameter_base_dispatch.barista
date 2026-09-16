abstract class Base:
	abstract func accept_type(_klass: Type[Self]) -> void


class Child:
	extends Base

	func accept_type(_klass: Type[Child]) -> void:
		pass


func test() -> void:
	var base: Base = Child.new()
	base.accept_type(Base)
