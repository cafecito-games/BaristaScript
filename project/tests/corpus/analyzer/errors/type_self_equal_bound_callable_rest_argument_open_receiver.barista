# A callable's variadic tail holds `Self` exactly as its fixed parameters do, one nesting level
# further in, so a tail written against the calling frame's receiver is admissible only through that
# frame's own receiver. The unqualified calls below name it and stay accepted.
class Cell:
	func fan(_callback: Callable[[...Array[Self]], void]) -> void:
		pass

	func mixed(_callback: Callable[[int, ...Array[Self]], void]) -> void:
		pass

	func route_rest(other: Cell, callback: Callable[[...Array[Self]], void]) -> void:
		other.fan(callback)
		fan(callback)

	func route_mixed(other: Cell, callback: Callable[[int, ...Array[Self]], void]) -> void:
		other.mixed(callback)
		mixed(callback)


func test() -> void:
	pass
