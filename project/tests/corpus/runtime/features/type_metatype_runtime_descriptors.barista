var stored_node_class: Type[Node]


func expect_node_class(klass: Type[Node]) -> void:
	print(klass == Node)


func return_node_class() -> Type[Node]:
	return Node


func test():
	expect_node_class(Node)
	stored_node_class = Node
	print(stored_node_class == Node)
	print(return_node_class() == Node)
