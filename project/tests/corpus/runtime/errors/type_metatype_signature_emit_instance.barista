# A handle-typed signal parameter rejects an instance at runtime, even when the value reaches emit
# through a Variant the analyzer cannot narrow.
signal registered(factory: Type[RefCounted])


func test():
	var value: Variant = RefCounted.new()
	emit_signal("registered", value)
	print("not ok")
