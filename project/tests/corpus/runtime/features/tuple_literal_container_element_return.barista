# A return type types a tuple literal returned against it, in a function and in a lambda alike.
func returns_pair() -> (int, Array[int]):
	return (1, [])


func test():
	var from_function := returns_pair()
	print(from_function)
	print(from_function[1].get_typed_builtin() == TYPE_INT)

	var make_pair := func() -> (int, Array[int]):
		return (2, [3])
	var from_lambda: (int, Array[int]) = make_pair.call()
	print(from_lambda)
	print(from_lambda[1].get_typed_builtin() == TYPE_INT)
