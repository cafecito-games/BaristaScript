enum OrCaseMessage:
	Quit
	Move(x: int, y: int)

func test():
	var message: OrCaseMessage = OrCaseMessage.Quit
	if message is OrCaseMessage.Move(x, y) or true:
		print(x)
		print(y)
