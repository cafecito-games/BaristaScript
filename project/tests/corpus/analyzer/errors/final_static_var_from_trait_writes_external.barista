# A flattened trait method that writes a *different* class's `final static var` must be rejected just
# as outside a trait: the other class owns its static final's single slot. The qualified reference is
# unambiguously external, so it is flagged rather than treated as the implementer's own slot.
extends RefCounted
uses CafecitoStaticWriter

func test() -> void:
	pass
