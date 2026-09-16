# The analyzer applies the same gradual rule the runtime does: an argument-erased source satisfies a
# specialized declaration, with no `Variant` anywhere in the flow. `Box.new()` has static type `Box`,
# and storing it into a `Box[int]` declaration produces no diagnostic, even though `Box.new() is
# Box[int]` is false.
class Box[T]:
	var value: T


trait Keeper[T]:
	func label() -> String:
		return "keeper"


class ForwardingKeeper[U]:
	uses Keeper[U]


func test() -> void:
	var erased_class := Box.new()
	var class_slot: Box[int] = erased_class

	var erased_trait := ForwardingKeeper.new()
	var trait_slot: Keeper[int] = erased_trait

	print(class_slot != null)
	print(trait_slot != null)
	print("specialized store accepts erased source ok")
