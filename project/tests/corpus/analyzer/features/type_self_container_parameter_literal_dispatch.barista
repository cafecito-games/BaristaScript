# A container literal written in this frame from `self` is a carrier of this frame's receiver: its
# element type is this frame's `Self`, so the array the running frame builds is typed with the leaf
# the receiver actually has. Passing it to an `Array[Self]` parameter of *this* receiver is therefore
# admitted, and the override that narrows the parameter to the leaf class accepts the carrier at
# runtime. A carrier built for another instance's `Self` stays rejected -- see
# `analyzer/errors/type_self_foreign_receiver_argument.fs`.
class Base:
	func call_items() -> void:
		take_items([self])
		take_mapping({ "item": self })

	func take_items(_items: Array[Self]) -> void:
		pass

	func take_mapping(_mapping: Dictionary[String, Self]) -> void:
		pass


class Child:
	extends Base

	func take_items(items: Array[Child]) -> void:
		print(items[0] is Child)

	func take_mapping(mapping: Dictionary[String, Child]) -> void:
		print(mapping["item"] is Child)


func test() -> void:
	var base: Base = Child.new()
	base.call_items()
