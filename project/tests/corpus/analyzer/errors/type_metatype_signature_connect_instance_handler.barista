# A handler whose parameter is an instance type cannot be connected to a signal parameter declared
# as a class handle, and the reverse is rejected too.
class Factory extends RefCounted:
	pass


signal registered(factory: Type[Factory])
signal produced(factory: Factory)


func on_instance(_factory: Factory) -> void:
	pass


func on_handle(_factory: Type[Factory]) -> void:
	pass


func test() -> void:
	var _connected: int = connect("registered", on_instance)
	var _also_connected: int = connect("produced", on_handle)
