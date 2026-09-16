# A trait can carry both a blank `final var` and the `_init` that fills it. When the implementing
# class adds no `_init` of its own, the trait's flattened `_init` is the assignment slot, so the blank
# final is definitely assigned and the class compiles.
extends RefCounted
uses HasId

trait HasId:
	final var id: int
	func _init() -> void:
		id = 5

func test() -> void:
	print(id)
