class Base:
	func make_via_initializer() -> Self:
		var tmp: Self = Base.new()
		return tmp

	func make_via_assignment() -> Self:
		var tmp: Self = self
		tmp = Base.new()
		return tmp


class Child:
	extends Base


func test() -> void:
	pass
