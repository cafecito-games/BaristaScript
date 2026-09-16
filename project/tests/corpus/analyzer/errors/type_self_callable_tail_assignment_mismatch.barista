# The reconciliation that admits a gradual tail into a `Self`-bearing destination does not admit an
# unrelated or narrower one: a declaration, an assignment, and a return type each check the tail the
# same way a parameter does.
class Cell:
	func declare_wrong(wrong: Callable[[...Array[String]], void]) -> void:
		var declared: Callable[[...Array[Self]], void] = wrong
		print(declared)

	func assign_wrong(wrong: Callable[[...Array[String]], void], narrow: Callable[[...Array[Leaf]], void]) -> void:
		var slot: Callable[[...Array[Self]], void] = wrong
		slot = narrow
		print(slot)

	func return_wrong(wrong: Callable[[...Array[String]], void]) -> Callable[[...Array[Self]], void]:
		return wrong


class Leaf:
	extends Cell


func test() -> void:
	pass
