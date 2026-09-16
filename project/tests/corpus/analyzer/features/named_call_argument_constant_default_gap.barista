func combine(a: int, b: int = 10, c: int = 20) -> int:
	return a + b + c

func multi(a: int, b: int = 1, c: int = 2, d: int = 3) -> int:
	return a * 1000 + b * 100 + c * 10 + d

func typed_default(a: int, b: float = 2, c: int = 0) -> float:
	return a + b + c

const PAIR: Array[int] = [10, 20]

func const_container(a: int, b: Array[int] = PAIR, c: int = 0) -> int:
	return a + b[0] + b[1] + c

func test():
	# Skip a middle defaulted parameter whose default is a compile-time constant.
	# `b` is filled from its constant default (10); `c` keeps the named value.
	print(combine(1, c = 5))
	# Same gap fill when the leading argument is also named.
	print(combine(a = 1, c = 5))
	# Two consecutive middle gaps (`b` and `c`) filled from constant defaults.
	print(multi(0, d = 9))
	# A constant default of a different builtin type is converted on the
	# constant-argument path, just like a written literal would be.
	print(typed_default(1, c = 5))
	# A constant container default is inlined and read just like the callee's
	# own default would supply it.
	print(const_container(1, c = 5))
