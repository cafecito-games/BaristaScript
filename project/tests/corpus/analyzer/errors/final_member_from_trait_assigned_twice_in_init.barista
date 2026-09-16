# The single assignment slot of a trait-supplied blank `final var` is filled by the first `_init`
# write; a second write in `_init` is a double-assignment.
extends RefCounted
uses HasId

trait HasId:
	final var id: int

func _init() -> void:
	id = 1
	id = 2

func test() -> void:
	pass
