# A tuple literal that is an element of an array or a dictionary is typed at construction, so reading
# it back into a declared tuple slot later stores instead of failing at a distance.
func test():
	var rows: Array[(int, Array[int])] = [(1, [])]
	print(rows)
	var first: (int, Array[int]) = rows[0]
	print(first[1].get_typed_builtin() == TYPE_INT)

	var map: Dictionary[String, (int, Array[int])] = {"a": (1, [2])}
	print(map)
	var entry: (int, Array[int]) = map["a"]
	print(entry[1].get_typed_builtin() == TYPE_INT)
