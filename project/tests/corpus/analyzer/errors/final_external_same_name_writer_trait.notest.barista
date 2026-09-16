# Helper trait for final_member_from_trait_writes_same_named_external. Supplies its own `final var
# value` yet writes a *different* class's same-named `final` from a concrete method; the external
# write must still be rejected even though the name collides with this trait's own final.
trait_name CafecitoSameNameWriter

final var value: int = 1

func poke(b: CafecitoLockedBox) -> void:
	b.value = 7
