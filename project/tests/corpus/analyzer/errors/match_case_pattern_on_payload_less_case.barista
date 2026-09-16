enum PayloadLessPatternMessage:
	Quit
	Move(x: int, y: int)

func test():
	var message: PayloadLessPatternMessage = PayloadLessPatternMessage.Quit
	match message:
		PayloadLessPatternMessage.Quit(x):
			print(x)
