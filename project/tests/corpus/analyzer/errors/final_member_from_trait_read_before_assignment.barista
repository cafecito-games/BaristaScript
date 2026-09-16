# A non-final trait variable initializer runs during construction, before the implementer's `_init()`
# fills a trait-supplied blank final, so reading that final from such an initializer is a
# use-before-assignment.
extends RefCounted
uses HasId

trait HasId:
	final var id: int
	var copy := id

func _init() -> void:
	id = 1

func test() -> void:
	pass
