# Tagged-union values compare by value: equality is the deep Array comparison, so two values of the
# same case with equal payloads are equal however they were obtained, values of different cases never
# are, and every value hashes by content and is usable as a Dictionary key.
enum RuntimeMessage:
	Quit
	Move(x: int, y: int)
	Write(text: String)

func quit_case() -> RuntimeMessage:
	return RuntimeMessage.Quit

func move_case(x: int, y: int) -> RuntimeMessage:
	return RuntimeMessage.Move(x, y)

func test():
	print(quit_case() == RuntimeMessage.Quit)
	print(move_case(1, 2) == RuntimeMessage.Move(1, 2))
	print(move_case(1, 2) == RuntimeMessage.Move(2, 1))
	print(RuntimeMessage.Write("hello") == RuntimeMessage.Write("hello"))
	print(RuntimeMessage.Write("hello") == RuntimeMessage.Write("world"))

	# Different cases of the same union are never equal, whatever their payloads.
	print(RuntimeMessage.Quit == RuntimeMessage.Move(1, 2))
	print(RuntimeMessage.Quit != RuntimeMessage.Move(1, 2))
	print(RuntimeMessage.Move(1, 2) == RuntimeMessage.Write("hello"))

	var labels := {}
	labels[RuntimeMessage.Quit] = "quit"
	labels[RuntimeMessage.Move(1, 2)] = "move"
	labels[RuntimeMessage.Write("hello")] = "write"
	print(labels[quit_case()])
	print(labels[move_case(1, 2)])
	print(labels[RuntimeMessage.Write("hello")])
	print(labels.size())
	print(labels.has(RuntimeMessage.Move(2, 1)))
