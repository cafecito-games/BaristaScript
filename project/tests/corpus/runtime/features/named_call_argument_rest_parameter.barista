# Named arguments may target the declared fixed parameters that precede a rest
# parameter. The rest parameter itself cannot be named, but it still collects
# any trailing positional arguments.

func collect(first: int, second: int, ...rest: Array) -> void:
	prints(first, second, rest)

func test():
	# Fixed parameters named and reordered. No positional argument can follow a
	# named one, so the rest parameter stays empty.
	collect(second = 2, first = 1)
	# A leading positional argument followed by a named fixed parameter. The rest
	# parameter is still empty.
	collect(1, second = 2)
	# All arguments positional: the fixed parameters bind in order and the rest
	# parameter collects the trailing values.
	collect(1, 2, 3, 4)
