# A `Self`-bearing callable type is a destination in a declaration and a return type just as it is in a
# parameter, and the same reconciliation applies: a gradual tail accepts whatever the slot will send
# it, so it is admissible in all three. These destinations name no receiver expression, so receiver
# identity never arises for them; only the shape has to agree.
class Cell:
	func sink(...values: Array) -> void:
		print("sink ", values.size())

	func fan(callback: Callable[[...Array[Self]], void]) -> void:
		callback.call(self, self)

	func stored_tail() -> Callable[[...Array[Self]], void]:
		var gradual: Callable[[...Array], void] = sink
		return gradual

	func run() -> void:
		var gradual: Callable[[...Array], void] = sink
		var declared: Callable[[...Array[Self]], void] = gradual
		var assigned: Callable[[...Array[Self]], void] = sink
		assigned = gradual
		fan(declared)
		fan(assigned)
		fan(stored_tail())


func test() -> void:
	Cell.new().run()
