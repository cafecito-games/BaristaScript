# Helper trait for final_member_from_trait_writes_nonself_shadowed_final. Declares a `final var` and a
# concrete method that writes it through a *non-self* receiver typed as this trait. The receiver may be
# any implementer whose trait-supplied final is still in effect, so the write must be rejected even
# when the implementer that flattens this method shadows the final with a mutable member.
trait_name CafecitoShadowedFinalTrait

final var id: int = 1

func poke(other: CafecitoShadowedFinalTrait) -> void:
	other.id = 9
