# A static lambda's captured receiver is reified into every runtime type position the #1546 resolver
# handles, including typed containers. Reading the element/value script back off the produced
# collection is the check: a `Variant` or bare-class substitution would leave the container untyped or
# typed against the declaring class.
class Base:
	static func make_collector() -> Callable:
		return func() -> Array[Self]:
			var items: Array[Self] = []
			items.push_back(Self.new())
			return items

	static func make_registry() -> Callable:
		return func() -> Dictionary[String, Self]:
			var table: Dictionary[String, Self] = {}
			table["first"] = Self.new()
			return table


class Child:
	extends Base


func test() -> void:
	# D. `Array[Self]` built by an escaped lambda carries the captured receiver as its element script.
	var collector: Callable = Child.make_collector()
	var items: Array = collector.call()
	print(items.get_typed_script() == Child)

	var base_collector: Callable = Base.make_collector()
	var base_items: Array = base_collector.call()
	print(base_items.get_typed_script() == Base)

	# D. `Dictionary[String, Self]` reifies the captured receiver into the value script.
	var registry: Callable = Child.make_registry()
	var table: Dictionary = registry.call()
	print(table.get_typed_value_script() == Child)
