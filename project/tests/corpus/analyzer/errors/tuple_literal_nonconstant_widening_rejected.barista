# A typed-array store converts, so a non-constant `int` element widens into an `Array[float]`. A tuple
# store never converts, so the same non-constant element is rejected for a `float` tuple slot. Only a
# constant, whose exact value is known at construction, is widened into a tuple element.
func supply_int() -> int:
	return 1


func test():
	var widening_array: Array[float] = [supply_int()]
	print(widening_array)

	var slot: (float, int) = (supply_int(), 2)
	print(slot)
