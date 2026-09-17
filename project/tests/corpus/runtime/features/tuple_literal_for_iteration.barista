# A `for` loop's declared iteration type types the tuple literals written in its list.
func test():
	for entry: (int, Array[int]) in [(1, []), (2, [3])]:
		print(entry, " ", entry[1].get_typed_builtin() == TYPE_INT)
