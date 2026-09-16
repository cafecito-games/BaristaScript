# Reading (not writing) a trait-supplied final from a flattened trait method is allowed; only writes
# outside the single slot are violations.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 7
	func doubled() -> int:
		return id + id

func test() -> void:
	print(doubled())
