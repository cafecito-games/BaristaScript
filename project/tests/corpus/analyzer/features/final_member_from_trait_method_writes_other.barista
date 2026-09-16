# A trait method that writes a *different* object's member (`other.id`) must not be mistaken for a
# write to the implementer's own final of the same name; only `self`/bare references are the slot.
extends RefCounted
uses HasId

class Other:
	var id: int = 0

trait HasId:
	final var id: int = 1
	func poke(o: Other) -> void:
		o.id = 7

func test() -> void:
	var o := Other.new()
	poke(o)
	print(id, " ", o.id)
