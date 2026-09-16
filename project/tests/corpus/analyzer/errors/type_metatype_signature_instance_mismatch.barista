# A signature taking class handles and a signature taking instances of the same class are not
# interchangeable in either direction.
class Factory extends RefCounted:
	static func describe() -> String:
		return "factory"


func handle_taker(_factory: Type[Factory]) -> void:
	pass


func instance_taker(_factory: Factory) -> void:
	pass


func test() -> void:
	var expects_handle: Callable[[Type[Factory]], void] = self.instance_taker
	var expects_instance: Callable[[Factory], void] = self.handle_taker
	print(expects_handle)
	print(expects_instance)
