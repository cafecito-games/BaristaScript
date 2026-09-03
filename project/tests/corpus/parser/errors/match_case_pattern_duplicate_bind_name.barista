enum DuplicatePatternBindMessage:
	Quit
	Move(x: int, y: int)

func test():
	var message: DuplicatePatternBindMessage = DuplicatePatternBindMessage.Quit
	match message:
		DuplicatePatternBindMessage.Move(x, x):
			print(x)
