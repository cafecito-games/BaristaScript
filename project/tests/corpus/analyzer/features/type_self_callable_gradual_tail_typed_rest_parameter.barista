# A gradual variadic tail (`...Array`) accepts every trailing argument, so it satisfies a parameter
# whose tail is typed with `Self`: whatever the callee sends, the callback takes. Such a callback binds
# no receiver anywhere, so there is no caller-relative `Self` for receiver identity to protect and the
# admission holds through a foreign open receiver as well as the calling frame's own. Both dispatch
# with the receiving frame's instances.
class Cell:
	func fan(callback: Callable[[...Array[Self]], void]) -> void:
		callback.call(self, self)

	func sink(...values: Array) -> void:
		print("sink ", values.size())

	func run(other: Cell) -> void:
		var gradual: Callable[[...Array], void] = sink
		fan(gradual)
		other.fan(gradual)


func test() -> void:
	Cell.new().run(Cell.new())
