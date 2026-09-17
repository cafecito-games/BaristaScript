# Enum methods stay available on a tagged union: an instance method receives the case value as `self`
# and can inspect it by comparing against cases, including bare case names from inside the enum's own
# declaration, while a static method is reached through the enum name.
enum RuntimeMessage:
	Quit
	Move(x: int, y: int)

	func describe() -> String:
		if self == RuntimeMessage.Quit:
			return "quit"
		if self == RuntimeMessage.Move(1, 2):
			return "move 1 2"
		return "other"

	func is_quit() -> bool:
		return self == Quit

	static func default_message() -> RuntimeMessage:
		return RuntimeMessage.Quit

func test():
	print(RuntimeMessage.Quit.describe())
	print(RuntimeMessage.Move(1, 2).describe())
	print(RuntimeMessage.Move(3, 4).describe())
	print(RuntimeMessage.Quit.is_quit())
	print(RuntimeMessage.Move(1, 2).is_quit())
	print(RuntimeMessage.default_message() == RuntimeMessage.Quit)

	var message: RuntimeMessage = RuntimeMessage.Move(1, 2)
	print(message.describe())
	print(message.is_quit())
