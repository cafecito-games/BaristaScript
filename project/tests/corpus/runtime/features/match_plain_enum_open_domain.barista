# An enum-typed slot can hold an integer outside the declared values, and both match spellings must
# route it to the catch-all instead of falling through.
enum Level:
	LOW = 1
	HIGH = 2


func by_values(value: Level) -> String:
	match value:
		Level.LOW:
			return "low"
		Level.HIGH:
			return "high"
		_:
			return "undeclared"


func by_type(value: Level) -> String:
	match value:
		value is Level:
			return "declared"
		_:
			return "undeclared"


func test():
	prints(by_values(Level.LOW), by_values(99 as Level))
	prints(by_type(Level.HIGH), by_type(99 as Level))
