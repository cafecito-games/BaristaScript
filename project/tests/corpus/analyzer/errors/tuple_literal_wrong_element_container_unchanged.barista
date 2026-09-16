# An element that is already a typed container of the wrong element type is not a literal being built,
# so nothing types it and the mismatch is reported as before.
func test():
	var strings: Array[String] = ["a"]
	var slot: (int, Array[int]) = (1, strings)
	print(slot)
