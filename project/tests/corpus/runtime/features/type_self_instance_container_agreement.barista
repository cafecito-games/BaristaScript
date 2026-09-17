class Base:
	var entries: Array[Self]
	var mapping: Dictionary[String, Self]

	func from_literal() -> Array[Self]:
		return [self]

	func from_local() -> Array[Self]:
		var out: Array[Self] = []
		out.append(self)
		return out

	func mapping_from_local() -> Dictionary[String, Self]:
		var out: Dictionary[String, Self] = {}
		out["self"] = self
		return out


class Child:
	extends Base


func test() -> void:
	var child := Child.new()
	child.entries.append(child)
	child.mapping["self"] = child
	print("literal: ", child.from_literal().get_typed_script() == Child)
	print("local: ", child.from_local().get_typed_script() == Child)
	print("mapping: ", child.mapping_from_local().get_typed_value_script() == Child)
	print("member: ", child.entries.get_typed_script() == Child)
	print("member mapping: ", child.mapping.get_typed_value_script() == Child)

	var base := Base.new()
	base.entries.append(base)
	print("base literal: ", base.from_literal().get_typed_script() == Base)
	print("base member: ", base.entries.get_typed_script() == Base)
