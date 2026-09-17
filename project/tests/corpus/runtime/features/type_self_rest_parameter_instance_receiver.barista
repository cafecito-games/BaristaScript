# An inherited instance method's rest tail resolves `Self` against the running instance's leaf script,
# the same way its static counterpart resolves against the invoked class handle.
class Base:
	func take(...values: Array[Self]) -> void:
		print("instance rest is Child: %s" % [values.get_typed_script() == Child])

	func take_items(items: Array[Self]) -> void:
		print("instance declared is Child: %s" % [items.get_typed_script() == Child])


class Child extends Base:
	pass


func test() -> void:
	var receiver: Variant = Child.new()
	receiver.take(Child.new())
	var items: Array[Child] = [Child.new()]
	receiver.take_items(items)
