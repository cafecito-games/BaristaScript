# A `Self` position nested inside another container is typed against the receiver exactly like a
# single-level one, so nested literals returned from `-> Array[Array[Self]]` and
# `-> Array[Dictionary[String, Self]]` analyze cleanly and reify against the leaf.
class Base:
	func grid() -> Array[Array[Self]]:
		return [[self]]

	func deep_grid() -> Array[Array[Array[Self]]]:
		return [[[self]]]

	func rows() -> Array[Dictionary[String, Self]]:
		return [{ "item": self }]

	func keyed_rows() -> Array[Dictionary[Self, String]]:
		return [{ self: "item" }]


class Child extends Base:
	pass


func test() -> void:
	var child := Child.new()
	var grid := child.grid()
	print("inner array is Child typed: %s" % [grid[0].get_typed_script() == Child])
	print("nested element is Child: %s" % [grid[0][0] is Child])
	var deep_grid := child.deep_grid()
	print("innermost array is Child typed: %s" % [deep_grid[0][0].get_typed_script() == Child])
	print("innermost element is Child: %s" % [deep_grid[0][0][0] is Child])
	var rows := child.rows()
	print("nested dictionary is Child typed: %s" % [rows[0].get_typed_value_script() == Child])
	print("nested dictionary value is Child: %s" % [rows[0]["item"] is Child])
	var keyed_rows := child.keyed_rows()
	print("nested dictionary key is Child typed: %s" % [keyed_rows[0].get_typed_key_script() == Child])
