# A dictionary element of a tuple literal takes both declared slots, empty or not.
func test():
	var filled: (int, Dictionary[String, int]) = (1, {"a": 1})
	print(filled)
	print(filled[1].get_typed_key_builtin() == TYPE_STRING)
	print(filled[1].get_typed_value_builtin() == TYPE_INT)

	var empty: (int, Dictionary[String, int]) = (1, {})
	print(empty)
	print(empty[1].get_typed_key_builtin() == TYPE_STRING)
	print(empty[1].get_typed_value_builtin() == TYPE_INT)
