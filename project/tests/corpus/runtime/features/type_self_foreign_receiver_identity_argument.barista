# A `Self` parameter position names the callee frame's own receiver, so on an open receiver the only
# foreign value that satisfies it is the receiver expression itself, at every nesting depth an
# anonymous tuple carries the position inward. Calls through the calling frame's own receiver keep
# passing the current `self`, whose `Self` is the same value the callee resolves.
class Cell:
	var label: String = "?"

	func take(other: Self) -> void:
		print("took ", other.label)

	func take_pair(pair: (int, Self)) -> void:
		print("took pair ", pair.0, " ", pair.1.label)

	func route(other: Cell) -> void:
		other.take(other)
		other.take_pair((1, other))
		take(self)
		self.take(self)
		take_pair((2, self))
		self.take_pair((3, self))


class Derived extends Cell:
	pass


func test() -> void:
	var derived := Derived.new()
	derived.label = "derived"
	var plain := Cell.new()
	plain.label = "plain"
	derived.route(plain)
