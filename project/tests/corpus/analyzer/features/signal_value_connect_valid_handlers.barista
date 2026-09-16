# Correctly typed handlers connect through both the Signal-value spelling and the string-based
# `Object.connect()` spelling, including a lambda handler.
signal registered(node: Node)

var seen: Array[String] = []


func on_registered(node: Node) -> void:
	seen.append("method:" + node.name)


func on_registered_again(node: Node) -> void:
	seen.append("string:" + node.name)


func test() -> void:
	registered.connect(on_registered)
	connect("registered", on_registered_again)
	registered.connect(func(emitted: Node) -> void:
		seen.append("lambda:" + emitted.name))

	var node := Node.new()
	node.name = "Probe"
	registered.emit(node)
	print(seen)
	node.free()
