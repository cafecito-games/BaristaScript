var stored_node_class: Type[Node]


func expect_node_class(klass: Type[Node]) -> void:
	print(klass == Node)


func return_node_class() -> Type[Node]:
	var klass: Variant = Node
	return klass


func test():
	var klass: Variant = Node
	expect_node_class(klass)
	stored_node_class = klass
	print(stored_node_class == Node)
	print(return_node_class() == Node)
