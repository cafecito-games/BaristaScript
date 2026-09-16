# A blank `final var` supplied by a trait is write-once on the implementing class: assigning it
# exactly once in the implementer's `_init()` is the legal single write, read-only afterward.
extends RefCounted
uses HasId

trait HasId:
	final var id: int

func _init() -> void:
	id = 42

func test() -> void:
	print(id)
