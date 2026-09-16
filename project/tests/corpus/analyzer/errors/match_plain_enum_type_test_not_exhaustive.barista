# `value is Level` is a membership test over the declared values, so it fails for an undeclared
# integer in an enum-typed slot and cannot close the match.
enum Level:
	LOW = 1
	HIGH = 2


func describe(value: Level) -> String:
	match value:
		value is Level:
			return "declared"


func test():
	print(describe(Level.LOW))
