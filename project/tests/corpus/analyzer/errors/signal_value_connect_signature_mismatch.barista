# Connecting through the Signal value itself (`registered.connect(...)`) validates the handler's
# signature, just like the `Object.connect("registered", ...)` spelling does.
signal registered(node: Node)


func on_registered(resource: Resource) -> void:
	print(resource)


func test() -> void:
	registered.connect(on_registered)
