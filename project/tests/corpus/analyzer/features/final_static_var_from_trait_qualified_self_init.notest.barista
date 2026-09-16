# Helper trait for final_static_var_from_trait_qualified_self_init. Fills its own blank `final static
# var` exactly once in `_static_init` through a qualified `Self.COUNT` reference; a static final has a
# single shared slot, so the qualified form is the slot itself and must be accepted.
trait_name CafecitoRegistry

final static var COUNT: int

static func _static_init() -> void:
	CafecitoRegistry.COUNT = 3
