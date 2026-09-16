# create_proxy[T] only accepts a trait or abstract type, mirroring the runtime
# guard. A concrete type such as `Node` is rejected statically.
func handle(_method_name: StringName, _args: Array) -> Variant:
	return null

func test() -> void:
	var bad := create_proxy[Node](handle)
	print(bad)
