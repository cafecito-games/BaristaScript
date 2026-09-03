enum ShadowPatternBindMessage:
	Quit
	Move(x: int, y: int)

func test():
	var x: int = 1
	var message: ShadowPatternBindMessage = ShadowPatternBindMessage.Quit
	match message:
		ShadowPatternBindMessage.Move(x, y):
			prints(x, y)
