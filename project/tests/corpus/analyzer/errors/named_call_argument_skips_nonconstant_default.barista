func make_default() -> int:
	return 10

func combine(a: int, b: int = make_default(), c: int = 20) -> int:
	return a + b + c

func test():
	# `b` is skipped but its default is not a compile-time constant, so it
	# cannot be inlined at the call site.
	print(combine(1, c = 5))
