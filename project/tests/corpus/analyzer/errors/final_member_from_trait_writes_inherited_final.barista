# A flattened trait method that writes an *inherited* final (reached through the trait's base
# constraint) must be rejected on an implementer that inherits the same base: the base owns the
# single slot, so the write outside `_init` is a write-once violation.
extends CafecitoFinalBaseClass
uses CafecitoInheritedWriter

func test() -> void:
	pass
