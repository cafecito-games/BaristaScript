# When every alternative claiming a literal's form needs `Self` resolved, none of them can answer the
# literal as it was reduced, so leaving it alone would reject a literal one of them accepts. The
# alternative whose elements the literal could stand in is chosen instead, and `Self` then resolves
# against the calling frame's own receiver exactly as it does for a lone `Array[Self]` annotation.
class Receiver:
	func absorb(_entries: Array[Self] | Array[(int, Self)]) -> void:
		pass

	func drive() -> void:
		absorb([self])
		absorb([(1, self)])
		print("absorbed")


func test() -> void:
	Receiver.new().drive()
