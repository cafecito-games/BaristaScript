func identity[T](value: T) -> T:
	return value

func test():
	var signed_narrow: int = 1
	var signed_wide: long = 1L
	var unsigned_narrow: uint = 1U
	var unsigned_wide: ulong = 18446744073709551615UL
	print(signed_narrow)
	print(signed_wide)
	print(unsigned_narrow)
	print(unsigned_wide)

	# A signed and an unsigned constant of the same value are distinct constants, so the compiled
	# constant pool must not merge them and hand the second spelling the first one's carrier.
	print(typeof(1))
	print(typeof(1U))
	print(typeof(1L))
	print(typeof(1UL))

	# Every type position resolves the same four spellings.
	print(identity[int](1))
	print(identity[uint](1U))
	print(identity[long](1L))
	print(identity[ulong](1UL))
	print(signed_wide is long)
	print(signed_wide as long)
