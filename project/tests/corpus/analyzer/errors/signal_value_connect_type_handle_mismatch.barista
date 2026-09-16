# A class-handle signal parameter is not satisfied by an instance-typed handler parameter through
# the Signal-value `connect()` spelling.
class Factory extends RefCounted:
	pass


signal registered(factory: Type[Factory])


func on_registered(factory: Factory) -> void:
	print(factory)


func test() -> void:
	registered.connect(on_registered)
