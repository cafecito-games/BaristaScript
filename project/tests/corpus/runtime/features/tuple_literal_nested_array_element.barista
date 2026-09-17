# Both nesting levels of an array-of-array element are typed.
func test():
	var rows: (int, Array[Array[int]]) = (1, [[2]])
	print(rows)
	print(rows[1].get_typed_builtin() == TYPE_ARRAY)
	print(rows[1][0].get_typed_builtin() == TYPE_INT)
