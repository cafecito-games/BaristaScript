# A local variable in a flattened trait method that happens to share a trait final's name is an
# ordinary local, not the final; reassigning it must not be flagged as a write-once violation.
extends RefCounted
uses HasId

trait HasId:
	final var id: int = 1
	func compute() -> int:
		var id := 10
		id += 5
		return id

func test() -> void:
	print(compute())
