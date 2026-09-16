# A plain enum is an open, integer-backed domain, so listing every declared member leaves the
# undeclared carrier values unhandled and the function without a return on that path.
enum Level:
	LOW = 1
	HIGH = 2


func describe(value: Level) -> String:
	match value:
		Level.LOW:
			return "low"
		Level.HIGH:
			return "high"


func test():
	print(describe(Level.LOW))
