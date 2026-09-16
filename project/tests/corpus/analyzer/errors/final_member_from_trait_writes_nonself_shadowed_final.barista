# Shadowing a trait's `final var` with a mutable member only makes *this* instance's slot mutable; a
# flattened trait method that writes the final through a non-self receiver typed as the trait still
# targets a final on that other instance, so the write must be rejected.
extends RefCounted
uses CafecitoShadowedFinalTrait

var id: int = 5

func test() -> void:
	pass
