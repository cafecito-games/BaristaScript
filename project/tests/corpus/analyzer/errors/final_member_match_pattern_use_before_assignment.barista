# A `match` pattern expression can reference a final; reading a blank final from a
# pattern is a use-before-assignment.
final var id: int

func _init(value: int) -> void:
	match value:
		self.id:
			id = 1
		_:
			id = 2

func test() -> void:
	pass
