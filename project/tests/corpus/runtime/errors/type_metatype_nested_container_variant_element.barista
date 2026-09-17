# An explicitly typed `Array[Type[Node]]` still rejects an instance funneled through `Variant`, so the
# element rule is enforced at runtime and not only by the analyzer.
func test():
	var instance: Variant = RefCounted.new()
	var nodes: Array[Type[Node]] = []
	nodes.append(instance)
	print("not ok")
