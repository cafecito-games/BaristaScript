# An instance receiver whose static type is the leaf resolves the callee's `Self` positions against
# that leaf, so leaf-typed arguments are accepted directly instead of having to be routed through a
# `Variant` handle to escape analysis. The rest tail is part of the same signature and resolves the
# same way as a fixed parameter.
class Base:
	func take(...values: Array[Self]) -> void:
		print("rest tail is Child: %s" % [values.get_typed_script() == Child])

	func take_one(value: Self) -> void:
		print("fixed parameter is Child: %s" % [value is Child])

	func take_items(items: Array[Self]) -> void:
		print("declared array is Child: %s" % [items.get_typed_script() == Child])


class Child extends Base:
	pass


func test() -> void:
	var child := Child.new()
	child.take(Child.new(), Child.new())
	child.take_one(Child.new())
	var items: Array[Child] = [Child.new()]
	child.take_items(items)
