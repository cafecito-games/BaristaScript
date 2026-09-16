# A concrete trait method that reassigns a trait-supplied `final static var` is flattened into the
# implementing class and must be rejected, as the slot is filled during static initialization.
extends RefCounted
uses HasRegistry

trait HasRegistry:
	final static var REGISTRY := {}
	static func wipe() -> void:
		REGISTRY = {}

func test() -> void:
	pass
