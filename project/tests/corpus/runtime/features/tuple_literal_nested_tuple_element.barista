# A tuple literal nested inside a tuple literal is typed through the outer element type, at either
# position and to any depth.
func test():
	var inner_second: (int, (int, Array[int])) = (1, (2, []))
	print(inner_second)
	print(inner_second[1][1].get_typed_builtin() == TYPE_INT)

	var mixed: (int, (String, Array[int])) = (1, ("a", []))
	print(mixed)
	print(mixed[1][1].get_typed_builtin() == TYPE_INT)

	var inner_first: ((int, Dictionary[String, int]), int) = ((1, {"a": 1}), 2)
	print(inner_first)
	print(inner_first[0][1].get_typed_value_builtin() == TYPE_INT)
