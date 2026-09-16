# A union's alternatives are types in their own right, so the `Self` contract compares them as such.
# Comparing the member vector in one step stops at each alternative's kind and carrier, where two
# callable alternatives with different signatures read as one type -- and a callee would then invoke
# the supplied callback with the wrong argument type. This holds whether `Self` stands beside the union
# or is reachable only from inside it.
class Receiver:
	func take_beside(_pair: (int | Callable[[int], void], Self)) -> void:
		pass

	func take_within(_pair: (int | Callable[[Self], void], bool)) -> void:
		pass

	func drive(bad: int | Callable[[String], void]) -> void:
		take_beside((bad, self))
		take_within((bad, true))


func test() -> void:
	pass
