# An initialized final fills its slot in the declaration and is read-only after.
class Named:
	final var name := "default"

	func describe() -> String:
		return name

func test() -> void:
	var n := Named.new()
	print(n.describe())
