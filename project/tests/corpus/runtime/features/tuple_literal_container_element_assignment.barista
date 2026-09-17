# The declared type of the assignee types a tuple literal written on the right-hand side, both after
# an initialized declaration and after a bare one.
func test():
	var initialized: (int, Array[int]) = (1, [4])
	initialized = (1, [])
	print(initialized)
	print(initialized[1].get_typed_builtin() == TYPE_INT)

	var declared: (int, Array[int])
	declared = (1, [5])
	print(declared)
	print(declared[1].get_typed_builtin() == TYPE_INT)
