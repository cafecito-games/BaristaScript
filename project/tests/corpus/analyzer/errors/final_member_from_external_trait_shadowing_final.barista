# An implementing class that shadows an external trait's plain `var` with its own `final var` makes
# the flattened slot write-once; the trait's concrete method then writes that final outside `_init`,
# which must be rejected even though the trait body bound the reference to its own mutable member.
extends RefCounted
uses CafecitoExtMutableTrait

final var id: int = 9

func test() -> void:
	print(id)
