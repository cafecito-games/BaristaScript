# Mutating the same backing store through an untyped alias bypasses every analyzer check, so the
# rejection here comes from the container descriptor itself.
func test():
	var nodes: Array[Type[Node]] = []
	var untyped: Array = nodes
	untyped.append(RefCounted.new())
	print(nodes.size())
	untyped.append(Resource)
	print(nodes.size())
	untyped.append(42)
	print(nodes.size())
	untyped.append(Button)
	print(nodes.size())
