# The `self.`-qualified spelling of a call denotes the same call as the unqualified one, so a
# receiver already typed `Self` contributes the frame's own `Self` to the parameter contract instead
# of nesting `Self` inside itself. Every spelling below supplies the calling frame's receiver.
class Cell:
	func take_all(items: Array[Self]) -> void:
		prints("took", items.size(), items[0] == self)

	func unqualified() -> void:
		var items: Array[Self] = [self]
		take_all(items)

	func qualified() -> void:
		var items: Array[Self] = [self]
		self.take_all(items)

	func route_unqualified() -> void:
		take_all([self])

	func route_qualified() -> void:
		self.take_all([self])

	func capture_untyped() -> void:
		var callable := Callable(self, "take_all")
		callable.call([self])


func test() -> void:
	var cell := Cell.new()
	cell.unqualified()
	cell.qualified()
	cell.route_unqualified()
	cell.route_qualified()
	cell.capture_untyped()
