# A `match` with a wildcard branch that assigns on every branch makes the blank
# final definitely assigned.
class Categorized:
	final var kind: String

	func _init(value: int) -> void:
		match value:
			0:
				kind = "zero"
			1:
				kind = "one"
			_:
				kind = "many"

func test() -> void:
	print(Categorized.new(0).kind)
	print(Categorized.new(5).kind)
