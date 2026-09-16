# The two admissions that predate the identity rule keep working for a nested `Self`: a receiver whose
# static class inherits the member without declaring it has `Self` substituted to that class before
# the argument is checked, and a `final` class has exactly one possible binding for `Self`, so an
# ordinary value of that class is admitted whether or not it is the receiver.
class Receiver:
	func take_pair(pair: (int, Self)) -> void:
		print("pair ", pair.0)


class Sub:
	extends Receiver


final class Closed:
	final func take_pair(pair: (int, Self)) -> void:
		print("closed ", pair.0)


func test() -> void:
	var sub := Sub.new()
	sub.take_pair((1, sub))

	var closed := Closed.new()
	var other_closed := Closed.new()
	closed.take_pair((2, closed))
	closed.take_pair((3, other_closed))
