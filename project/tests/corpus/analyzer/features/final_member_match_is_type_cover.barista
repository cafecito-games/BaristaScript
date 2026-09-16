# A `match` whose only branch tests the subject against its whole domain always runs that branch,
# so the blank final it assigns is definitely assigned afterwards.
class Categorized:
	final var kind: String

	func _init(value: bool) -> void:
		match value:
			value is bool:
				kind = "flag:" + str(value)

func test() -> void:
	print(Categorized.new(true).kind)
	print(Categorized.new(false).kind)
