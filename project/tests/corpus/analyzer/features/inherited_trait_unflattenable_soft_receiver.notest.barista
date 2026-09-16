trait Namespace:
	class Helper:
		pass


class Base uses Namespace:
	pass


class Child extends Base:
	pass


func test() -> void:
	var soft_receiver = Child.new()
	soft_receiver.Helper()
