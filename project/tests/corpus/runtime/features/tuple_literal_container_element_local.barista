# An unnamed tuple literal takes its element types from the declared tuple type, so a container
# element is built already typed. A tuple store never converts, so this is what lets the store accept
# it at all.
func test():
	var empty: (int, Array[int]) = (1, [])
	print(empty)
	print(empty[1].get_typed_builtin() == TYPE_INT)

	var filled: (int, Array[int]) = (1, [2, 3])
	print(filled)
	print(filled[1].get_typed_builtin() == TYPE_INT)

	# An element that is already a typed container is unaffected.
	var typed: Array[int] = [4]
	var from_variable: (int, Array[int]) = (1, typed)
	print(from_variable)
	print(from_variable[1].get_typed_builtin() == TYPE_INT)
