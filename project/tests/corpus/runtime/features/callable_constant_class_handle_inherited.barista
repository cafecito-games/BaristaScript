class Base:
	static func spawn() -> int:
		return 7


class Child extends Base:
	pass


func test() -> void:
	var direct := Callable(Child, "spawn")
	var handle: Type[Child] = Child
	var indirect := Callable(handle, "spawn")
	print("inherited object == Child: ", direct.get_object() == Child)
	print("inherited method: ", direct.get_method())
	print("inherited result: ", direct.call())
	print("inherited equals indirect: ", direct == indirect)
