# Assigning a blank final on every arm of an `if`/`else` makes it definitely
# assigned at the join.
class Sign:
	final var label: String

	func _init(positive: bool) -> void:
		if positive:
			label = "pos"
		else:
			label = "neg"

func test() -> void:
	print(Sign.new(true).label)
	print(Sign.new(false).label)
