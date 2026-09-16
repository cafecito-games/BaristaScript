# A tagged union types its case values as values of the enum, not as integers: a payload-less
# case is a value on its own, a payload case is constructed with its declared field types, and
# both flow into variables, parameters, and returns typed as the union.
enum Command:
	Quit
	Move(x: int, y: int)
	Write(text: String)
	Scale(factor: float)

func quit_case() -> Command:
	return Command.Quit

func move_case() -> Command:
	return Command.Move(1, 2)

func write_case() -> Command:
	return Command.Write("hello")

func widened_argument() -> Command:
	# A float field accepts an int literal, widened at the construction site.
	return Command.Scale(2)

func assign_and_pass() -> Command:
	var message: Command = Command.Quit
	message = Command.Move(3, 4)
	return forward(message)

func forward(message: Command) -> Command:
	return message

func compare(left: Command, right: Command) -> bool:
	return left == right

func test():
	print(quit_case())
	print(move_case())
	print(write_case())
	print(widened_argument())
	print(assign_and_pass())
	print(compare(Command.Quit, Command.Quit))
	print(compare(Command.Quit, Command.Move(1, 2)))
