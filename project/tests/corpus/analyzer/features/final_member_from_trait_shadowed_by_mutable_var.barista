# When the implementing class declares its own member of the same name, that member shadows the
# trait's `final var` (matching the compiler's flattening), so the class field is an ordinary
# mutable variable and may be freely reassigned.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 1

var id: int = 2

func reassign() -> void:
	id = 3

func test() -> void:
	reassign()
	print(id)
