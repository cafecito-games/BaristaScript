# A fixed-width numeric shares its carrier with the wider type of the same signedness, so a callable's
# parameter width is part of its identity rather than of the carrier alone. Ordinary compatibility
# keeps those widths invariant in a signature, and a callable carrying `Self` answers the same way in
# every destination: neither the narrower nor the wider callback is substitutable, through either
# receiver, and the same holds for a declared slot.
class Cell:
	func want_long(_callback: Callable[[long, ...Array[Self]], void]) -> void:
		pass

	func want_int(_callback: Callable[[int, ...Array[Self]], void]) -> void:
		pass

	func route_narrow(other: Cell, narrow: Callable[[int, ...Array[Self]], void]) -> void:
		want_long(narrow)
		other.want_long(narrow)

	func route_wide(other: Cell, wide: Callable[[long, ...Array[Self]], void]) -> void:
		want_int(wide)
		other.want_int(wide)

	func declare(narrow: Callable[[int, ...Array[Self]], void]) -> void:
		var slot: Callable[[long, ...Array[Self]], void] = narrow
		print(slot)


func test() -> void:
	pass
