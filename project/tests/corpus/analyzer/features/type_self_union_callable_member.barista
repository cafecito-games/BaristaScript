# A callable alternative of a type union carries the `Self` rules of its own signature, rest tail
# included, while the plain alternative keeps accepting its own values. A method captured from the
# calling frame's own receiver satisfies the callable alternative.
class Cell:
	func fan(_callback: int | Callable[[...Array[Self]], void]) -> void:
		print("fanned")

	func take_bound(...values: Array[Cell]) -> void:
		print("bound ", values.size())

	func run() -> void:
		fan(3)
		fan(take_bound)


func test() -> void:
	Cell.new().run()
