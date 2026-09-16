# The static analog: a trait carries a blank `final static var` and the `_static_init` that fills
# it; the flattened `_static_init` is the static slot on each implementer.
extends RefCounted
uses HasRegistry

trait HasRegistry:
	final static var COUNT: int
	static func _static_init() -> void:
		COUNT = 3

func test() -> void:
	print(COUNT)
