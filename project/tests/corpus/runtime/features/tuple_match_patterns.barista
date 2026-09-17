# A parenthesized pattern list matches a tuple element by element. Sub-patterns are ordinary patterns,
# so they can be literals, binds, wildcards or nested tuple and array patterns. A single parenthesized
# pattern is a grouping, not a one-element tuple.
tuple Pair(first: int, second: int)

func quadrant(point: (int, int)) -> String:
	match point:
		(0, 0):
			return "origin"
		(0, var y):
			return "y axis %d" % y
		(var x, 0):
			return "x axis %d" % x
		(var x, var y) when x > 0 and y > 0:
			return "first %d,%d" % [x, y]
		_:
			return "elsewhere"

func describe_nested(value: ((int, int), String)) -> String:
	match value:
		((0, 0), var label):
			return "origin " + label
		((var x, var y), "skip"):
			return "skipped %d,%d" % [x, y]
		_:
			return "other"

func test():
	print(quadrant((0, 0)))
	print(quadrant((0, 3)))
	print(quadrant((4, 0)))
	print(quadrant((2, 5)))
	print(quadrant((-1, -2)))

	print(describe_nested(((0, 0), "here")))
	print(describe_nested(((7, 8), "skip")))
	print(describe_nested(((7, 8), "keep")))

	# A named tuple erases to the same Array shape, so it matches the same patterns.
	var pair: Pair = Pair(5, 6)
	match pair:
		(var first, var second):
			prints("pair", first, second)

	# Because tuples erase to read-only Arrays, an array pattern of the same arity also matches them.
	match (1, 2):
		[var a, var b]:
			prints("array pattern", a, b)

	# A grouped pattern is the pattern itself, binds included.
	match 3:
		(3):
			print("grouped literal")
	match 7:
		(var grouped_bind):
			print(grouped_bind)

	# The rest pattern only exists for arrays; a tuple has a fixed arity.
	match (1, 2, 3):
		(1, var mid, 3):
			prints("middle", mid)
