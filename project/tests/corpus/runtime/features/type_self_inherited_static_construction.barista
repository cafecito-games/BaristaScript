trait Factory:
	abstract static func make() -> Self


extend RefCounted uses Factory:
	static func make() -> Self:
		return Self.new()


class Base:
	static func spawn() -> Self:
		return Self.new()


class Derived:
	extends Base


func test() -> void:
	var made: Resource = Resource.make()
	print(made is Resource, " ", made.get_class())
	print(RefCounted.make().get_class())
	print(Derived.spawn() is Derived, " ", Base.spawn() is Derived)
