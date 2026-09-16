func test():
	# An explicit cast keeps truncation toward zero for the values the destination can hold.
	print(3.9 as int)
	print(-3.9 as int)
	print(4000000000.0 as long)

	# A checked operation folds every result that fits its type, exactly.
	print(2000000000U + 100U)
	print(9223372036854775807L - 1L)
	print(1UL << 63UL)
	print(3L ** 39L)

	# Complement is taken at the declared width, so "uint" flips exactly its own 32 bits.
	print(~0U)
