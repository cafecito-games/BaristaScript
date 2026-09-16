# A trait's own final-enforcement pass is skipped (trait members are checked on each implementer), so
# a `final` member declared with a getter in a trait must still be rejected on the implementing class.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 1: get = _get_id
	func _get_id() -> int:
		return 1

func test() -> void:
	pass
