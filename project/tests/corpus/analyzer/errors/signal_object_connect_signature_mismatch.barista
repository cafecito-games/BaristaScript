# The string-based `Object.connect()` spelling reports the same diagnostic as the Signal-value
# spelling in `signal_value_connect_signature_mismatch.fs`, so the two paths cannot drift apart.
signal registered(node: Node)


func on_registered(resource: Resource) -> void:
	print(resource)


func test() -> void:
	connect("registered", on_registered)
