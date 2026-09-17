func test():
	var klass: Variant = Node
	print(klass is Type[Node])

	var casted := klass as Type[Node]
	print(casted == Node)

	var resource_class: Variant = Resource
	var node_instance := Node.new()
	var node_instance_dynamic: Variant = node_instance
	print(resource_class is Type[Node])
	print(node_instance_dynamic is Type[Node])
	print(resource_class as Type[Node] == null)
	print(node_instance_dynamic as Type[Node] == null)
	node_instance.free()
