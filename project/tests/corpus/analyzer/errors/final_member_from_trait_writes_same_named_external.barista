# Even when an applied trait supplies a `final` of the same name, a flattened trait method that writes
# a *different* class's same-named `final` must be rejected: name overlap alone must not mask a write
# to a genuinely external final, which is identified by its declaring node, not its name.
extends RefCounted
uses CafecitoSameNameWriter

func test() -> void:
	pass
