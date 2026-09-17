class Child:
	func check() -> void:
		var child_class: Variant = Child
		var other_class: Variant = Other

		print(child_class is Type[Self])
		print(other_class is Type[Self])
		print(child_class as Type[Self] == Child)
		print(other_class as Type[Self] == null)


class Other:
	pass


func test() -> void:
	Child.new().check()
