# The unqualified construction spelling runs against the constructing frame's own receiver, so a
# carrier typed with that frame's `Self` is the carrier the tuple's and the payload's `Self` fields
# resolve to. The frame runs on a subclass leaf, which is what the carrier reifies to.
class Base:
	tuple Crate(index: int, items: Array[Self])

	enum Message:
		Empty
		Load(index: int, items: Array[Self])

	func build() -> void:
		var items: Array[Self] = [self]
		var crate := Crate(1, items)
		var _message := Message.Load(2, items)
		print("crate ", crate.index, " ", crate.items.size())
		print("payload built")


class Child:
	extends Base


func test() -> void:
	Child.new().build()
