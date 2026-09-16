# A `match` without a wildcard branch leaves a no-match path open, so the blank
# final is not definitely assigned.
final var id: int

func _init(value: int) -> void:
	match value:
		0:
			id = 10
		1:
			id = 20

func test() -> void:
	pass
