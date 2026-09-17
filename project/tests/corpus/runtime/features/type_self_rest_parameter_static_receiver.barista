class Base:
	static func take(...values: Array[Self]) -> void:
		print("rest element is Child: %s" % [values.get_typed_script() == Child])

	static func take_first(_first: Self, ...values: Array[Self]) -> void:
		print("control element is Child: %s" % [values.get_typed_script() == Child])


class Child extends Base:
	pass


func test() -> void:
	var handle: Variant = Child
	handle.take(Child.new())
	handle.take_first(Child.new(), Child.new())
