# A flattened trait method that writes a *different* class's `final` member must be rejected exactly
# as the same write from a non-trait method would be: the other class owns its final's single slot.
# This regresses the cross-class case, which name-based slot resolution alone would miss.
extends RefCounted
uses CafecitoBoxWriter

func test() -> void:
	pass
