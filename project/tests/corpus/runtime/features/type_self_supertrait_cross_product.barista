trait Root:
	abstract static func make() -> Self


trait Leaf:
	uses Root


extend RefCounted uses Leaf:
	static func make() -> Self:
		return Self.new()


func test() -> void:
	var made: Resource = Resource.make()
	print(made.get_class())
