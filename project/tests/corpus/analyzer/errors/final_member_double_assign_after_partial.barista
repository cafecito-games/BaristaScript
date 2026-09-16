# A final assigned on only one arm of an `if` may already be assigned when control
# reaches a later unconditional write, making that write a possible double-assignment.
final var id: int

func _init(flag: bool) -> void:
	if flag:
		id = 1
	id = 2

func test() -> void:
	pass
