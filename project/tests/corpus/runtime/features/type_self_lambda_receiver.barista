class Base:
	func spawn_via_lambda() -> Callable:
		return func() -> Self:
			return Self.new()


class Derived:
	extends Base


func test() -> void:
	var derived := Derived.new()
	var factory := derived.spawn_via_lambda()
	print(factory.call() is Derived)
