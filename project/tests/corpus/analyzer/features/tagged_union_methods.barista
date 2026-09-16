# Enum methods stay available on a tagged union. An instance method sees `self` as a case value
# of the union, and a static method is reached through the enum name.
enum Command:
	Quit
	Move(x: int, y: int)

	func describe() -> String:
		return "message"

	static func default_message() -> Command:
		return Command.Quit

func describe_case(message: Command) -> String:
	return message.describe()

func describe_constructed() -> String:
	return Command.Move(1, 2).describe()

func from_static() -> Command:
	return Command.default_message()

func test():
	print(describe_case(Command.Quit))
	print(describe_constructed())
	print(from_static() == Command.Quit)
