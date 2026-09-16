# Capturing a member function as a value stamps the same receiver contract a call does, so the
# `self.`-qualified capture and the local it is stored in carry exactly the signature the unqualified
# capture carries.
class Box[T]:
	var value: T


class Cell:
	func accept(box: Box[Self]) -> void:
		prints("accepted", box.value == self)

	func take(callback: Callable[[Box[Self]], void]) -> void:
		prints("took", callback.is_valid())

	func route_unqualified() -> void:
		take(accept)

	func route_qualified() -> void:
		take(self.accept)

	func route_local() -> void:
		var cb := self.accept
		take(cb)


func test() -> void:
	var cell := Cell.new()
	cell.route_unqualified()
	cell.route_qualified()
	cell.route_local()
