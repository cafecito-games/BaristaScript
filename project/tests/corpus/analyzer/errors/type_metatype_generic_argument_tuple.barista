# A tuple is a value shape erased to an Array, so there is no class to hold a handle for; the
# descriptor would be an Array carrying a flag core only honors for objects.
class Slot[T]:
	var value: T


func accept(slot: Slot[Type[(int, String)]]) -> void:
	print(slot)


func test():
	print(accept)
