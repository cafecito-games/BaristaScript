# An arity mismatch is rejected through the Signal-value `connect()` spelling.
signal registered(node: Node, index: int)


func on_registered(node: Node) -> void:
	print(node)


func test() -> void:
	registered.connect(on_registered)
