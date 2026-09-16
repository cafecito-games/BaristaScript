# A concrete trait method flattens into each implementing class, so a write it makes to a
# trait-supplied final outside the `_init` slot is a write-once violation even though the assignment
# is lexically inside the trait.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 1
	func mutate() -> void:
		id = 2

func test() -> void:
	pass
