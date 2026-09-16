# When an implementing class shadows a trait `final var` with its own mutable member, a flattened
# trait method that writes that name targets the class's mutable slot, not the dropped trait final,
# so the write is allowed. Member references inside a trait body bind per-implementer, so the
# write-once scan resolves them by name against the tracked finals, which excludes the shadowed one.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 1
	func mutate() -> void:
		id = 2

var id: int = 9

func test() -> void:
	mutate()
	print(id)
