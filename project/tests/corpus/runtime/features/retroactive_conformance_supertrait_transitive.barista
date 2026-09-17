# A retroactive conformance to a leaf trait implies every transitive supertrait for script, builtin,
# and native targets. A diamond reaches Root through two paths but still registers one membership.
trait Root:
	abstract func root_value() -> int


trait Left uses Root:
	pass


trait Right uses Root:
	pass


trait Leaf uses Left, Right:
	pass


trait Unrelated:
	pass


class ScriptTarget:
	pass


extend ScriptTarget uses Leaf:
	func root_value() -> int:
		return 11


extend int uses Leaf:
	func root_value() -> int:
		return self


extend RefCounted uses Leaf:
	func root_value() -> int:
		return 30


extend String uses Root:
	func root_value() -> int:
		return 40


func accepts_root(value: Root) -> int:
	return value.root_value()


func test() -> void:
	var script := ScriptTarget.new()
	var script_root: Root = script
	var script_wide: Variant = script
	print(script is Leaf)
	print(script is Left)
	print(script is Right)
	print(script is Root)
	print(script_wide is Root)
	print(is_instance_of(script_wide, Root))
	print((script as Root).root_value())
	print(accepts_root(script_root))

	var number := 7
	var number_root: Root = number
	var number_wide: Variant = number
	print(number is Leaf)
	print(number is Root)
	print(number_wide is Root)
	print(is_instance_of(number_wide, Root))
	@warning_ignore("unsafe_cast")
	var number_as_root := number_wide as Root
	print(number_as_root.root_value())
	print(accepts_root(number_root))

	var resource := Resource.new()
	var resource_root: Root = resource
	var resource_wide: Object = resource
	print(resource is Leaf)
	print(resource is Root)
	print(resource_wide is Root)
	print(is_instance_of(resource_wide, Root))
	print((resource as Root).root_value())
	print(accepts_root(resource_root))
	var native_base := Object.new()
	var native_base_wide: Variant = native_base
	print(native_base_wide is Root)
	native_base.free()
	var unrelated_node := Node.new()
	var unrelated_native: Variant = unrelated_node
	print(unrelated_native is Root)
	unrelated_node.free()

	var root_only := "root"
	var root_only_wide: Variant = root_only
	print(root_only is Root)
	print(root_only_wide is Leaf)
	print(root_only_wide is Unrelated)
