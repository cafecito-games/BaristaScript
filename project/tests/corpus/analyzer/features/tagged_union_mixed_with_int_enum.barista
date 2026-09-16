# A tagged union and a classic int-backed enum coexist: the int enum keeps folding its values
# into integers, while the tagged union's cases stay case values.
enum Severity:
	LOW = 0
	HIGH = 1

enum Command:
	Quit
	Move(x: int, y: int)

func int_enum_is_still_an_int() -> int:
	return Severity.HIGH

func tagged_union_case() -> Command:
	return Command.Move(1, 2)

func both(level: Severity, message: Command) -> String:
	return str(level) + str(message)

func test():
	print(int_enum_is_still_an_int())
	print(tagged_union_case())
	print(both(Severity.HIGH, Command.Move(1, 2)))
