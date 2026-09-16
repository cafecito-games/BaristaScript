# Calling the result of an index expression is an error in every context. The parser
# now lets `name[...](...)` through so call reduction can interpret use-site type
# arguments; until generic-method application lands this stays a call on an expression,
# reported exactly once per call site (no duplicate parser + analyzer diagnostics).
func values():
	return [1]


func test():
	var a = [1]
	a[0]()
	var _b = a[0]()
	values()[0]()


func from_return():
	var a = [1]
	return a[0]()
