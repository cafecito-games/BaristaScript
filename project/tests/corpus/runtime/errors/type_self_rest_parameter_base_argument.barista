class Base:
	static func take(..._values: Array[Self]) -> void:
		print("body ran")


class Child extends Base:
	pass


func test() -> void:
	print("before")
	var handle: Variant = Child
	handle.take(Base.new())
