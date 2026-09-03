enum EmptyPayloadPatternMessage:
	Quit
	Move(x: int, y: int)

func test():
	var message: EmptyPayloadPatternMessage = EmptyPayloadPatternMessage.Quit
	match message:
		EmptyPayloadPatternMessage.Move():
			print("move")
