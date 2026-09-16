# A `final var` declared in a trait flattens into each implementing class, so reassigning it from a
# method of the implementer (outside its `_init` slot) is a write-once violation, just like a
# directly declared final.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 1

func mutate() -> void:
	id = 2

func test() -> void:
	pass
