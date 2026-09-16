# Helper trait for final_member_from_trait_writes_external_final. Its concrete method writes another
# class's `final` member; the write is resolved in this trait's own context against CafecitoLockedBox.
trait_name CafecitoBoxWriter

func poke(b: CafecitoLockedBox) -> void:
	b.value = 7
