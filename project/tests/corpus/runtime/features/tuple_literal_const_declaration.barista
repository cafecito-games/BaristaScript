# A `const` tuple is folded into the constant pool, so its elements have to be typed before the fold
# or the bytecode would carry an untyped container forever.
const SLOT: (int, Array[int]) = (1, [2])


func test():
	print(SLOT)
	print(SLOT[1].get_typed_builtin() == TYPE_INT)
	print(SLOT[1].is_read_only())
