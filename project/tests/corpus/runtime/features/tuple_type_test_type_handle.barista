# A `Type[T]` tuple element tests class handles rather than instances, so the distinction has to
# survive into the runtime tuple shape: the handle passes and an instance of the same class does not.
func test():
	var handles: Variant = (Node, 1)
	print(handles is (Type[Node], int))
	print(handles is (Node, int))

	var instance := Node.new()
	var instances: Variant = (instance, 1)
	print(instances is (Type[Node], int))
	print(instances is (Node, int))
	instance.free()
