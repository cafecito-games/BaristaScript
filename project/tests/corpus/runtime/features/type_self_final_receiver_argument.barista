# A `final` receiver has exactly one possible run-time class, so its `Self` positions are proven by the
# ordinary final-type rules and take any value of that class -- no receiver identity required.
final class Sealed:
	var label: String = "?"

	func take(other: Self) -> void:
		print("sealed took ", other.label)

	func take_pair(pair: (int, Self)) -> void:
		print("sealed took pair ", pair.0, " ", pair.1.label)


func test() -> void:
	var first := Sealed.new()
	first.label = "first"
	var second := Sealed.new()
	second.label = "second"
	first.take(second)
	first.take_pair((1, second))
