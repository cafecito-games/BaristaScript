# An implementing class that shadows an external trait's `final var` with its own mutable member makes
# the flattened slot mutable; the trait's concrete method writes that name and must not be flagged,
# even though the trait body resolved the reference against its own (now shadowed) final member.
extends RefCounted
uses CafecitoExtFinalTrait

var id: int = 9

func test() -> void:
	print(id)
